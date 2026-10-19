// =============================================================================
// src/codegen/src/emit/worker.cpp - `worker(...)` lowering (spec 13), in two
// halves:
//
//   * the ENTRY unit: `worker(data)` lowers to
//     `::cp3::runtime::worker(data, &::<worker-namespace>::cp3_default)`, and
//     the generated source includes the worker module's generated header (the
//     CLI compiles the worker TU from the same import, see the cli module);
//     the argument is a primitive value, a worker-marked object binding, or a
//     worker-marked object literal (a `std::tuple`), because the runtime
//     deep-copies the argument;
//   * the WORKER unit (options.workerModule): only `export default (…) => …`
//     lowers, as `cp3_default(params) { … }`; an import or any other top-level
//     statement reports CP3002 so a worker the entry cannot drive stays a skip.
//
// The pre-scan that marks the object literals lowering to tuples (the
// `worker(...)` kernel form) lives in emit/worker_scan.cpp; this unit emits
// them and gates everything else.
// =============================================================================
#include "detail/generator.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "detail/cpp_writer.h"
#include "detail/mangle.h"
#include "detail/type_map.h"

namespace cp3 {

using namespace detail;

namespace {

// The property name of a plain object key, or "".
std::string propertyKeyName(const Expr& key) {
    if (key.kind == NodeKind::Identifier) {
        return static_cast<const Identifier&>(key).name;
    }
    if (key.kind == NodeKind::StringLiteral) {
        return static_cast<const StringLiteral&>(key).value;
    }
    return std::string();
}

}  // namespace

// -----------------------------------------------------------------------------
// The entry unit: `worker(data)`
// -----------------------------------------------------------------------------

std::string Generator::emitWorkerCall(const CallExpr& c) {
    if (!options_.worker.has_value()) {
        notImplemented(c, "worker(...)");
        return "0.0";
    }
    if (!c.typeArgs.empty()) {
        notImplemented(c, "worker(...) with type arguments");
        return "0.0";
    }
    if (c.arguments.size() != 1 || !c.arguments.front() ||
        c.arguments.front()->kind == NodeKind::SpreadElement) {
        notImplemented(c, "worker(...) outside the worker(value) form");
        return "0.0";
    }
    const std::string arg = workerArgText(*c.arguments.front());
    if (arg.empty()) return "0.0";  // workerArgText reported the shape
    needsWorker_ = true;
    return "::cp3::runtime::worker(" + arg + ", &::" +
           suggestNamespace(options_.worker->moduleName) + "::cp3_default)";
}

std::string Generator::workerArgText(const Expr& arg) {
    if (arg.kind == NodeKind::ObjectLiteral &&
        workerLiterals_.count(arg.nodeId) != 0) {
        const std::string text =
            emitWorkerObjectLiteral(static_cast<const ObjectLiteral&>(arg));
        if (!text.empty()) return text;
    }
    const Type t = effectiveType(arg);
    // A worker-marked object binding: its literal already lowered to the
    // tuple, so the argument is its plain value.
    if (t.kind == TypeKind::Object && workerObjectLabels_.count(t.name) != 0) {
        return expr(arg);
    }
    const CppInfo ci = classify(t);
    if (!ci.representable || !isPrimitiveShape(ci.shape)) {
        notImplemented(arg, "this worker(...) argument type");
        return std::string();
    }
    return expr(arg);
}

// A worker-marked object literal -> `std::tuple<...>{...}`. The gate is the
// same narrow one the enum-carrying literals use: plain keys, non-spread
// values, primitive-representable fields, no duplicates; anything else stays
// CP3002 (the caller reports).
std::string Generator::emitWorkerObjectLiteral(const ObjectLiteral& ol) {
    if (workerLiterals_.count(ol.nodeId) == 0) return std::string();
    std::vector<std::string> names;
    std::vector<std::string> types;
    std::vector<std::string> values;
    for (const auto& propertyPtr : ol.properties) {
        const ObjectProperty* p = propertyPtr.get();
        if (p == nullptr || p->computed || p->isMethod || !p->key || !p->value ||
            p->value->kind == NodeKind::SpreadElement) {
            return std::string();
        }
        const std::string name = propertyKeyName(*p->key);
        if (name.empty()) return std::string();
        for (const std::string& existing : names) {
            if (existing == name) return std::string();
        }
        const CppInfo field = info(*p->value);
        if (!field.representable || !isPrimitiveShape(field.shape)) {
            return std::string();
        }
        names.push_back(name);
        types.push_back(field.cpp);
        values.push_back(expr(*p->value));
    }
    std::string out =
        "std::tuple<" + joinStrings(types, ", ") + ">{" + joinStrings(values, ", ") + "}";
    registerWorkerObject(effectiveType(ol), std::move(names));
    return out;
}

// `obj.field = value` over a worker object (a `std::tuple`): gated on the
// worker labels, so an ordinary tuple keeps its usual CP3002.
std::string Generator::emitWorkerMemberWrite(const AssignmentExpr& a) {
    if (!a.target || a.target->kind != NodeKind::MemberExpr ||
        a.op != AssignOp::Assign || !a.value) {
        return std::string();
    }
    const auto& m = static_cast<const MemberExpr&>(*a.target);
    if (!m.object) return std::string();
    const Type recv = effectiveType(*m.object);
    if (recv.kind != TypeKind::Object || workerObjectLabels_.count(recv.name) == 0) {
        return std::string();
    }
    const auto fields = enumObjectFields_.find(recv.name);
    if (fields == enumObjectFields_.end()) return std::string();
    for (std::size_t i = 0; i < fields->second.size(); ++i) {
        if (fields->second[i] == m.property) {
            return "(std::get<" + std::to_string(i) + ">(" + expr(*m.object) +
                   ") = " + expr(*a.value) + ")";
        }
    }
    return std::string();
}

// -----------------------------------------------------------------------------
// The worker unit: `export default (…) => …` -> `cp3_default(…) { … }`
// -----------------------------------------------------------------------------

void Generator::registerWorkerObject(const Type& objType,
                                     std::vector<std::string> names) {
    if (objType.kind != TypeKind::Object || objType.name.empty()) return;
    enumObjectFields_[objType.name] = std::move(names);
    workerObjectLabels_.insert(objType.name);
    needsTuple_ = true;
}

std::string Generator::workerParamText(const Parameter& p,
                                       const ArrowFunction& arrow,
                                       std::size_t index) {
    if (p.isRest) {
        notImplemented(p, "rest parameter");
        return std::string();
    }
    if (!p.name || p.name->kind != NodeKind::Identifier) {
        notImplemented(p, "destructuring parameter");
        return std::string();
    }
    const std::string name = mapIdent(static_cast<const Identifier&>(*p.name).name);
    std::optional<Type> pt;
    if (p.type) pt = typeOfNode(*p.type);
    if ((!pt || pt->isUnknown()) && sema_.functions.count(arrow.nodeId) != 0) {
        const auto& types = sema_.functions.at(arrow.nodeId).paramTypes;
        if (index < types.size()) pt = types[index];
    }
    declare(name, pt);
    if (p.type && p.type->kind == NodeKind::ObjectType) {
        return workerObjectParamText(static_cast<const ObjectType&>(*p.type), name, pt);
    }
    if (pt) {
        const CppInfo ci = classify(*pt);
        if (ci.representable && ci.shape != Shape::Void) return ci.cpp + " " + name;
    }
    notImplemented(p, "this worker parameter type");
    return std::string();
}

// `{ value: number }` -> `std::tuple<double> name`; registers the field names
// under the sema object label, so `obj.field` reads/writes lower to
// `std::get<i>` (the enum-literal tuple surface).
std::string Generator::workerObjectParamText(const ObjectType& ot,
                                             const std::string& name,
                                             const std::optional<Type>& pt) {
    if (!pt || pt->kind != TypeKind::Object || pt->name.empty()) {
        notImplemented(ot, "this worker object parameter");
        return std::string();
    }
    std::vector<std::string> names;
    std::vector<std::string> types;
    for (const auto& memberPtr : ot.members) {
        if (!memberPtr || memberPtr->kind != NodeKind::PropertySignature) {
            notImplemented(ot, "this worker object member");
            return std::string();
        }
        const auto& sig = static_cast<const PropertySignature&>(*memberPtr);
        if (sig.computed || !sig.key || !sig.type) {
            notImplemented(*memberPtr, "this worker object member");
            return std::string();
        }
        const std::string field = propertyKeyName(*sig.key);
        if (field.empty()) {
            notImplemented(*memberPtr, "this worker object member");
            return std::string();
        }
        for (const std::string& existing : names) {
            if (existing == field) return std::string();
        }
        auto fieldType = typeOfNode(*sig.type);
        if (!fieldType) {
            notImplemented(*sig.type, "this worker object member type");
            return std::string();
        }
        const CppInfo ci = classify(*fieldType);
        if (!ci.representable || !isPrimitiveShape(ci.shape)) {
            notImplemented(*memberPtr, "this worker object member type");
            return std::string();
        }
        names.push_back(field);
        types.push_back(ci.cpp);
    }
    std::string out = "std::tuple<" + joinStrings(types, ", ") + "> " + name;
    registerWorkerObject(*pt, std::move(names));
    return out;
}

std::string Generator::workerReturnType(const ArrowFunction& arrow) {
    if (arrow.returnType) {
        if (auto t = typeOfNode(*arrow.returnType)) {
            const CppInfo ci = classify(*t);
            if (ci.representable && ci.shape != Shape::Void) return ci.cpp;
        }
        notImplemented(arrow, "this worker return type");
        return std::string();
    }
    if (sema_.functions.count(arrow.nodeId) != 0) {
        const CppInfo ci = classify(sema_.functions.at(arrow.nodeId).returnType);
        if (ci.representable && ci.shape != Shape::Void) return ci.cpp;
    }
    return "void";
}

std::string Generator::workerBody(const ArrowFunction& arrow,
                                  const std::string& ret) {
    const std::string savedReturn = currentReturn_;
    const bool savedInAsync = inAsyncBody_;
    currentReturn_ = ret;
    inAsyncBody_ = false;
    ++nestingDepth_;
    std::string body;
    if (arrow.body && arrow.expressionBody) {
        body = "return " + expr(*static_cast<const Expr*>(arrow.body.get())) + ";";
    } else if (arrow.body) {
        stmt(*static_cast<const Stmt*>(arrow.body.get()), body, 1);
    }
    --nestingDepth_;
    inAsyncBody_ = savedInAsync;
    currentReturn_ = savedReturn;
    return body;
}

GeneratedModule Generator::runWorkerModule(const Program& program) {
    workerModule_ = true;
    const ExportDeclaration* def = nullptr;
    for (const auto& stmtPtr : program.body) {
        if (!stmtPtr) continue;
        if (stmtPtr->kind == NodeKind::ExportDeclaration) {
            const auto& ed = static_cast<const ExportDeclaration&>(*stmtPtr);
            if (ed.kind == ExportKind::Default && ed.defaultExpr && def == nullptr) {
                def = &ed;
                continue;
            }
            notImplemented(ed, "this worker-module export");
            return GeneratedModule{};
        }
        notImplemented(*stmtPtr, "this worker-module statement");
        return GeneratedModule{};
    }
    if (def == nullptr || !def->defaultExpr ||
        def->defaultExpr->kind != NodeKind::ArrowFunction) {
        notImplemented(def ? static_cast<const Node&>(*def)
                           : static_cast<const Node&>(program),
                       "a worker module without a default-export arrow");
        return GeneratedModule{};
    }
    const auto& arrow = static_cast<const ArrowFunction&>(*def->defaultExpr);
    if (arrow.isAsync || !arrow.typeParams.empty()) {
        notImplemented(arrow, "this worker default export");
        return GeneratedModule{};
    }

    std::vector<std::string> params;
    params.reserve(arrow.params.size());
    for (std::size_t i = 0; i < arrow.params.size(); ++i) {
        if (!arrow.params[i]) continue;
        std::string text = workerParamText(*arrow.params[i], arrow, i);
        if (text.empty()) return GeneratedModule{};
        params.push_back(std::move(text));
    }
    const std::string ret = workerReturnType(arrow);
    if (ret.empty()) return GeneratedModule{};
    const std::string signature =
        ret + " cp3_default(" + joinStrings(params, ", ") + ")";

    std::string decls;
    line(decls, 1, signature + ";");
    std::string defs;
    defs += signature + " {\n";
    defs += workerBody(arrow, ret);
    defs += "}\n";

    GeneratedModule module;
    module.header = buildHeader(decls);
    module.source = buildSource(decls, defs, std::string());
    return module;
}

}  // namespace cp3
