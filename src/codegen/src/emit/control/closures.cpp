// =============================================================================
// src/codegen/src/emit/closures.cpp - the closure (arrow) lowering
// (STYLE.md section 8).
//
// Two pieces of the closure surface live here:
//
//   * `isCell` - the shared-cell queries of spec section 10 "Closures": a
//     binding shared between a frame and a nested closure is a shared cell
//     exactly when it is also reassignable (cp3/core/value.hpp's codegen
//     contract). The lexical walk that finds those bindings lives in
//     support/captures.cpp (`analyzeCaptures`, detail/captures.h); this unit
//     consumes its result.
//   * the arrow helpers (`arrowParamType`, `arrowBodyText`): the parameter
//     types an arrow resolves and the body its lambda runs. They live here so
//     emit/async.cpp keeps the lambda assembly (the C++ signature, the
//     capture mode) in one place.
// =============================================================================
#include "detail/generator/generator.h"

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>

#include "detail/class/captures.h"
#include "detail/generator/cpp_writer.h"
#include "detail/generator/type_map.h"

namespace cp3 {

using namespace detail;

// Is `name` a shared cell of the current frame? A cell is emitted as
// `Cell<T> name`, read as `name.get()` and written through `name.set(...)`, so
// the enclosing frame and every capturing closure share one binding.
bool Generator::isCell(const std::string& name) const {
    const auto it = captures_.frameCells.find(currentFrameId_);
    return it != captures_.frameCells.end() && it->second.count(name) != 0;
}

// The cp3 type of an arrow's parameter: the annotation wins, then sema's
// resolved parameter type, then the type the context expects (the call
// context's callback signature).
std::optional<Type> Generator::arrowParamType(const ArrowFunction& a,
                                              std::size_t index,
                                              const std::optional<Type>& expectedFn) {
    const Parameter* p =
        index < a.params.size() ? a.params[index].get() : nullptr;
    if (p == nullptr) return std::nullopt;
    if (p->type) {
        if (auto t = typeOfNode(*p->type)) return t;
    }
    const auto fnIt = sema_.functions.find(a.nodeId);
    if (fnIt != sema_.functions.end() &&
        index < fnIt->second.paramTypes.size()) {
        return fnIt->second.paramTypes[index];
    }
    if (expectedFn && expectedFn->kind == TypeKind::Function &&
        index + 1 < expectedFn->parts.size()) {
        return expectedFn->parts[index];
    }
    return std::nullopt;
}

// The arrow's body statements, emitted in the arrow's own context: a lambda
// body returns from the lambda, so the enclosing frame's return type and
// frame id are swapped out. An async arrow's body is a coroutine body: its
// `return` yields with `co_return` and the body never falls off its end
// (the promise settles with the value type's default).
std::string Generator::arrowBodyText(const ArrowFunction& a, bool isAsync,
                                     const std::optional<Type>& expectedFn,
                                     std::string& signature) {
    // The arrow's value type; an async arrow's is its Promise's value type
    // (D40: the arrow is a coroutine whose promise type is `Promise<T>`).
    std::optional<Type> retCp = arrowReturnCpType(a, expectedFn);
    std::string bodyRet;
    if (isAsync) {
        Type value = retCp.value_or(Type::unknown());
        if (detail::isPromiseType(value)) value = detail::promiseValue(value);
        retCp = value;
        const CppInfo ci = classify(value);
        bodyRet = (ci.representable && ci.shape != Shape::Void) ? ci.cpp : "void";
        signature = "::cp3::runtime::Promise<" + bodyRet + ">";
    } else {
        signature = arrowReturnType(a, expectedFn);
        bodyRet = signature.empty() ? "void" : signature;
    }

    const std::string savedReturn = currentReturn_;
    const std::optional<Type> savedReturnType = currentReturnType_;
    const std::optional<Type> savedExpected = expected_;
    const std::uint32_t savedFrame = currentFrameId_;
    const bool savedInAsync = inAsyncBody_;
    currentReturn_ = bodyRet;
    currentReturnType_ = retCp;
    currentFrameId_ = a.nodeId;
    inAsyncBody_ = isAsync;
    expected_ = retCp;

    std::string body;
    // A destructuring parameter unpacks in the body prologue: the C++
    // parameter is the source value (the parameter list names it
    // `cp3_param<i>`).
    for (std::size_t i = 0; i < a.params.size(); ++i) {
        const Parameter* p = a.params[i].get();
        if (!p || !p->name) continue;
        const bool pattern = p->name->kind == NodeKind::ArrayPattern ||
                             p->name->kind == NodeKind::ObjectPattern;
        if (!pattern) continue;
        const std::optional<Type> pt = arrowParamType(a, i, expectedFn);
        const PatternSource src{"cp3_param" + std::to_string(i),
                                pt.value_or(Type::unknown()), false};
        unpackPattern(*p->name, src, body, 1);
    }
    if (a.body && a.webserverionBody) {
        body += (isAsync ? "co_return " : "return ") +
                expr(*static_cast<const Expr*>(a.body.get())) + ";";
    } else if (a.body) {
        stmt(*static_cast<const Stmt*>(a.body.get()), body, 1);
    }
    if (isAsync && !(a.body && a.webserverionBody)) {
        line(body, 1, bodyRet == "void" ? "co_return;" : "co_return {};");
    }

    expected_ = savedExpected;
    inAsyncBody_ = savedInAsync;
    currentFrameId_ = savedFrame;
    currentReturnType_ = savedReturnType;
    currentReturn_ = savedReturn;
    return body;
}

}  // namespace cp3
