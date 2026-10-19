// =============================================================================
// src/dap/src/session_queries.cpp - breakpoints, stack, variables (session.cpp half).
//
// OWNERSHIP: cp3-debug workstream (src/dap/**).
//
// Everything a stopped session can be asked for. Frames and variables come out
// .cp3-native because the Debug build maps generated lines back to the source
// (src/codegen/src/support/line_map.cpp), but this unit never assumes that: a
// frame without a source file is reported without one.
// =============================================================================
#include "detail/session.hpp"

#include <cstdlib>
#include <string>
#include <utility>

#include "detail/mi.hpp"

namespace cp3::dap {

int GdbSession::createScope(int frame, bool arguments) {
    Handle handle;
    handle.kind = arguments ? Handle::Kind::Arguments : Handle::Kind::Locals;
    handle.frame = frame;
    return intern(std::move(handle));
}

std::string GdbSession::variables(int reference, std::vector<VariableInfo>& out) {
    out.clear();
    const auto it = handles_.find(reference);
    if (it == handles_.end()) return "unknown variables reference";
    if (it->second.kind == Handle::Kind::Locals) {
        return listScope(it->second.frame, /*arguments=*/false, out);
    }
    if (it->second.kind == Handle::Kind::Arguments) {
        return listScope(it->second.frame, /*arguments=*/true, out);
    }
    return childrenFor(reference, out);
}

int GdbSession::intern(Handle handle) {
    const int reference = nextReference_++;
    handles_[reference] = std::move(handle);
    return reference;
}

std::string GdbSession::selectFrame(int frame) {
    if (selectedFrame_ == frame) return {};
    const MiResult result = command("-stack-select-frame " + std::to_string(frame));
    if (!result.ok()) return result.error;
    selectedFrame_ = frame;
    return {};
}

std::string GdbSession::setBreakpoints(
    const std::string& path, const std::vector<BreakpointRequest>& requests,
    std::vector<BreakpointState>& out) {
    out.clear();
    if (!launched_) return "the debug session has not been launched yet";
    std::vector<int>& numbers = breakpoints_[path];
    for (const int number : numbers) {
        command("-break-delete " + std::to_string(number));
    }
    numbers.clear();
    for (const BreakpointRequest& request : requests) {
        BreakpointState state;
        state.line = request.line;
        const MiResult result =
            command("-break-insert " +
                    miQuote(path + ":" + std::to_string(request.line)));
        MiValue value;
        std::size_t pos = 0;
        const MiValue* bkpt =
            result.ok() && parseMiBody(result.body, pos, value)
                ? miField(value, "bkpt")
                : nullptr;
        const std::string number = bkpt != nullptr ? miText(*bkpt, "number") : "";
        if (!number.empty()) {
            state.verified = true;
            state.gdbNumber = std::atoi(number.c_str());
            const std::string line = miText(*bkpt, "line");
            if (!line.empty()) {
                state.line = static_cast<std::uint32_t>(std::atoi(line.c_str()));
            }
            numbers.push_back(state.gdbNumber);
        } else if (!result.ok()) {
            state.message = result.error;
        } else {
            state.message = "gdb did not report a breakpoint number";
        }
        out.push_back(std::move(state));
    }
    return {};
}

std::string GdbSession::stackTrace(std::vector<FrameInfo>& out) {
    out.clear();
    if (!stopped_) return {};
    const MiResult result = command("-stack-list-frames");
    if (!result.ok()) return result.error;
    MiValue value;
    std::size_t pos = 0;
    if (!parseMiBody(result.body, pos, value)) return {};
    const MiValue* stack = miField(value, "stack");
    if (stack == nullptr) return {};
    for (const MiValue& item : stack->items) {
        if (item.kind != MiValue::Kind::Tuple) continue;
        FrameInfo frame;
        const std::string level = miText(item, "level");
        frame.level = level.empty() ? static_cast<int>(out.size())
                                    : std::atoi(level.c_str());
        frame.function = miText(item, "func");
        frame.path = miText(item, "fullname");
        if (frame.path.empty()) frame.path = miText(item, "file");
        const std::string line = miText(item, "line");
        if (!line.empty()) {
            frame.line = static_cast<std::uint32_t>(std::atoi(line.c_str()));
        }
        out.push_back(std::move(frame));
    }
    return {};
}

std::string GdbSession::listScope(int frame, bool arguments,
                                  std::vector<VariableInfo>& out) {
    if (!stopped_) return {};
    const std::string frameError = selectFrame(frame);
    if (!frameError.empty()) return frameError;
    const std::string level = std::to_string(frame);
    // Arguments come back wrapped: stack-args=[frame={level="N",args=[...]}],
    // while locals are a flat list. Asking for one explicit frame keeps the
    // wrapper shape independent of how gdb paginates frames.
    const std::string mi = arguments
        ? "-stack-list-arguments --all-values " + level + " " + level
        : "-stack-list-locals --all-values";
    const MiResult result = command(mi);
    if (!result.ok()) return result.error;
    MiValue value;
    std::size_t pos = 0;
    if (!parseMiBody(result.body, pos, value)) return {};
    const MiValue* entries = miField(value, arguments ? "stack-args" : "locals");
    if (arguments) {
        const MiValue* first =
            entries != nullptr && !entries->items.empty() ? &entries->items.front()
                                                          : nullptr;
        entries = first != nullptr ? miField(*first, "args") : nullptr;
    }
    if (entries == nullptr) return {};
    for (const MiValue& item : entries->items) {
        if (item.kind != MiValue::Kind::Tuple) continue;
        addVariable(miText(item, "name"), miText(item, "value"),
                    miText(item, "type"), frame, out);
    }
    return {};
}

void GdbSession::addVariable(const std::string& name, const std::string& value,
                             const std::string& type, int frame,
                             std::vector<VariableInfo>& out) {
    VariableInfo variable;
    variable.name = name;
    variable.value = value;
    variable.type = type;
    const char first = value.empty() ? '\0' : value[0];
    if (first == '{' || first == '[') {  // aggregates expand through a varobj
        Handle handle;
        handle.kind = Handle::Kind::Variable;
        handle.frame = frame;
        handle.expr = name;
        variable.expandable = true;
        variable.reference = intern(std::move(handle));
    }
    out.push_back(std::move(variable));
}

std::string GdbSession::childrenFor(int reference,
                                    std::vector<VariableInfo>& out) {
    const auto it = handles_.find(reference);
    if (it == handles_.end()) return "unknown variables reference";
    if (!stopped_) return {};
    const int frame = it->second.frame;
    std::string varobj = it->second.varobj;
    const std::string expr = it->second.expr;
    const std::string frameError = selectFrame(frame);
    if (!frameError.empty()) return frameError;
    if (varobj.empty()) {
        varobj = "cp3_v" + std::to_string(reference);
        const MiResult created =
            command("-var-create " + varobj + " * " + miQuote(expr));
        if (!created.ok()) return created.error;
        it->second.varobj = varobj;
    }
    const MiResult listed = command("-var-list-children --all-values " + varobj);
    if (!listed.ok()) return listed.error;
    MiValue value;
    std::size_t pos = 0;
    if (!parseMiBody(listed.body, pos, value)) return {};
    const MiValue* children = miField(value, "children");
    if (children == nullptr) return {};
    for (const MiValue& item : children->items) {
        if (item.kind != MiValue::Kind::Tuple) continue;
        VariableInfo child;
        child.name = miText(item, "exp");
        if (child.name.empty()) child.name = miText(item, "name");
        child.value = miText(item, "value");
        child.type = miText(item, "type");
        const std::string count = miText(item, "numchild");
        if (!count.empty() && std::atoi(count.c_str()) > 0) {
            Handle nested;
            nested.kind = Handle::Kind::Variable;
            nested.frame = frame;
            nested.varobj = miText(item, "name");
            child.expandable = true;
            child.reference = intern(std::move(nested));
        }
        out.push_back(std::move(child));
    }
    return {};
}

}  // namespace cp3::dap
