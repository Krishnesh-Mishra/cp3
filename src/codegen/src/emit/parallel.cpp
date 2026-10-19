// =============================================================================
// src/codegen/src/emit/parallel.cpp - `parallel.for` / `parallel.reduce` /
// `parallel.run` lowering (docs/language-spec/13-concurrency).
//
// `parallel` is an ambient core builtin used bare (`parallel.for(...)`); each
// entry point lowers onto the structured runtime surface in
// cp3/parallel/parallel.hpp:
//
//   parallel.for(range, body)       -> ::cp3::runtime::parallelFor(begin, end, body)
//   parallel.reduce(range, init, b) -> ::cp3::runtime::parallelReduce(begin, end,
//                                      init, b[, combine])
//   parallel.run(f1, ..., fN)       -> ::cp3::runtime::parallelRun(f1, ..., fN)
//
// The range bounds lower to `static_cast<std::int64_t>(expr)`; the closures
// lower through `emitParallelClosure`, which declares the closure's parameters
// with the cp3 types the callee hands them (a number index; the accumulator's
// type for a reduce) before lowering the arrow itself. A reduce body that is
// exactly one of the associative shapes (`acc + i` from 0, `acc * i` from 1,
// `i > acc ? i : acc` from 0) gets the chunked 5-argument lowering with a
// synthesized combine op; every other reduce keeps the sequential 4-argument
// form, which is still exact. Anything else reports CP3002 and emits a stub.
// =============================================================================
#include "detail/generator.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "cp3/diagnostic_codes.hpp"
#include "detail/cpp_writer.h"
#include "detail/parallel.h"
#include "detail/type_map.h"

namespace cp3 {

using namespace detail;

namespace {

// True when `e` is exactly the identifier `name` (no fallback lowering).
bool isNamed(const Expr* e, const std::string& name) {
    if (e == nullptr || e->kind != NodeKind::Identifier) return false;
    return static_cast<const Identifier&>(*e).name == name;
}

// A printable label for a diagnostic message: the nominal label when the type
// carries one (class, enum, type parameter), the kind name otherwise.
std::string parallelTypeLabel(const Type& t) {
    return t.name.empty() ? typeKindName(t.kind) : t.name;
}

// The two plain parameter names of a reduce body; false for anything else
// (rest/default/missing names), which keeps the shape out of the combine gate.
bool reduceParamNames(const ArrowFunction& body, std::string& accParam,
                      std::string& indexParam) {
    if (body.params.size() != 2 || !body.params[0] || !body.params[1]) return false;
    if (body.params[0]->isRest || body.params[1]->isRest) return false;
    const Identifier* acc = asIdentifier(body.params[0]->name.get());
    const Identifier* index = asIdentifier(body.params[1]->name.get());
    if (acc == nullptr || index == nullptr) return false;
    accParam = acc->name;
    indexParam = index->name;
    return true;
}

// `[](auto cp3_a, auto cp3_b) { return cp3_a <op> cp3_b; }`.
std::string combineOpText(const char* op) {
    return std::string("[](auto cp3_a, auto cp3_b) { return cp3_a ") + op +
           " cp3_b; }";
}

// `[](auto cp3_a, auto cp3_b) { return cp3_a > cp3_b ? cp3_a : cp3_b; }`.
const char* const kCombineMax =
    "[](auto cp3_a, auto cp3_b) { return cp3_a > cp3_b ? cp3_a : cp3_b; }";

// The synthesized combine op when the reduce body is one of the exact
// associative shapes and `init` is its identity literal; nullopt keeps the
// sequential 4-argument lowering (still correct, just not chunked).
std::optional<std::string> reduceCombine(const ArrowFunction& body,
                                         const Expr& init) {
    std::string accParam;
    std::string indexParam;
    if (!reduceParamNames(body, accParam, indexParam)) return std::nullopt;
    if (!body.expressionBody || !body.body) return std::nullopt;
    const Expr& value = *static_cast<const Expr*>(body.body.get());
    const std::optional<std::int64_t> identity = integralLiteralValue(init);
    if (!identity) return std::nullopt;
    if (value.kind == NodeKind::BinaryExpr) {
        const auto& b = static_cast<const BinaryExpr&>(value);
        if (!isNamed(b.left.get(), accParam) || !isNamed(b.right.get(), indexParam)) {
            return std::nullopt;
        }
        if (b.op == BinaryOp::Add && *identity == 0) return combineOpText("+");
        if (b.op == BinaryOp::Multiply && *identity == 1) return combineOpText("*");
        return std::nullopt;
    }
    if (value.kind == NodeKind::ConditionalExpr && *identity == 0) {
        const auto& c = static_cast<const ConditionalExpr&>(value);
        if (!c.condition || c.condition->kind != NodeKind::BinaryExpr) {
            return std::nullopt;
        }
        const auto& cmp = static_cast<const BinaryExpr&>(*c.condition);
        if (cmp.op != BinaryOp::GreaterThan) return std::nullopt;
        if (!isNamed(cmp.left.get(), indexParam) || !isNamed(cmp.right.get(), accParam)) {
            return std::nullopt;
        }
        if (!isNamed(c.whenTrue.get(), indexParam) ||
            !isNamed(c.whenFalse.get(), accParam)) {
            return std::nullopt;
        }
        return std::string(kCombineMax);
    }
    return std::nullopt;
}

}  // namespace

// -----------------------------------------------------------------------------
// The parallel.* call surface
// -----------------------------------------------------------------------------

// The cp3 result type of a `parallel.*` call: the reduce accumulator's type
// (the init, literal-widened) and the run marker (the common element type).
// `parallel.for` yields nothing and keeps the ambient unknown. Consulted by
// `effectiveType`/`bindingType` before sema's ambient result.
std::optional<Type> Generator::parallelCallType(const CallExpr& c) const {
    if (!c.callee || c.callee->kind != NodeKind::MemberExpr) return std::nullopt;
    const auto& m = static_cast<const MemberExpr&>(*c.callee);
    if (!m.object || m.object->kind != NodeKind::Identifier ||
        static_cast<const Identifier&>(*m.object).name != "parallel") {
        return std::nullopt;
    }
    if (m.property == "reduce") {
        if (c.arguments.size() != 3 || !c.arguments[1]) return std::nullopt;
        Type t = effectiveType(*c.arguments[1]);
        if (t.isLiteral()) t = t.literalBaseType();
        if (t.isUnknown() || t.isError()) return std::nullopt;
        return t;
    }
    if (m.property == "run") {
        // The element type comes from the first callable's resolved result;
        // the runtime unifies the rest with `std::common_type`.
        if (c.arguments.empty() || !c.arguments.front() ||
            c.arguments.front()->kind != NodeKind::ArrowFunction) {
            return std::nullopt;
        }
        const auto& arrow = static_cast<const ArrowFunction&>(*c.arguments.front());
        auto it = sema_.functions.find(arrow.nodeId);
        if (it == sema_.functions.end()) return std::nullopt;
        const Type& elem = it->second.returnType;
        if (elem.isUnknown() || elem.isError() || elem.isVoid()) return std::nullopt;
        return parallelRunType(elem);
    }
    return std::nullopt;
}

// `range` -> the two `std::int64_t` bound expressions; false when the argument
// is not a `start..end` range (the caller reports).
bool Generator::parallelRange(const Expr& range, std::string& begin,
                              std::string& end) {
    if (range.kind != NodeKind::RangeExpr) return false;
    const auto& r = static_cast<const RangeExpr&>(range);
    if (!r.start || !r.end) return false;
    begin = "static_cast<std::int64_t>(" + expr(*r.start) + ")";
    end = "static_cast<std::int64_t>(" + expr(*r.end) + ")";
    return true;
}

// The closure argument of a `parallel.*` call: the arrow lowers into a C++
// lambda, with the first `paramTypes.size()` parameters declared in the codegen
// env (their C++ spellings come from `contextParams`, else sema).
std::string Generator::emitParallelClosure(
    const ArrowFunction& a, const std::vector<Type>& paramTypes,
    const std::vector<std::string>& contextParams) {
    pushScope();
    for (std::size_t i = 0; i < a.params.size() && i < paramTypes.size(); ++i) {
        const Parameter* p = a.params[i].get();
        if (p && p->name && p->name->kind == NodeKind::Identifier) {
            declare(static_cast<const Identifier&>(*p->name).name, paramTypes[i]);
        }
    }
    std::string out = emitArrow(a, contextParams);
    popScope();
    return out;
}

// True when the body is a closure `parallel.*` can lower: not async, no type
// parameters, `count` plain identifier parameters.
bool Generator::parallelClosureShape(const ArrowFunction& a,
                                     std::size_t count) const {
    if (a.isAsync || !a.typeParams.empty() || a.params.size() != count) return false;
    for (const auto& param : a.params) {
        if (!param || param->isRest || !asIdentifier(param->name.get())) return false;
    }
    return true;
}

std::string Generator::emitParallelFor(const CallExpr& c) {
    if (c.arguments.size() != 2 || !c.arguments[0] || !c.arguments[1] ||
        c.arguments[1]->kind != NodeKind::ArrowFunction) {
        notImplemented(c, "parallel.for outside the (range, (i) => body) form");
        return "0.0";
    }
    const auto& body = static_cast<const ArrowFunction&>(*c.arguments[1]);
    if (!parallelClosureShape(body, 1)) {
        notImplemented(c, "this parallel.for closure shape");
        return "0.0";
    }
    std::string begin;
    std::string end;
    if (!parallelRange(*c.arguments[0], begin, end)) {
        notImplemented(c, "parallel.for over a non-range argument");
        return "0.0";
    }
    const std::string closure =
        emitParallelClosure(body, {Type::number()}, {"double"});
    return "::cp3::runtime::parallelFor(" + begin + ", " + end + ", " + closure +
           ")";
}

std::string Generator::emitParallelReduce(const CallExpr& c) {
    if (c.arguments.size() != 3 || !c.arguments[0] || !c.arguments[1] ||
        !c.arguments[2] || c.arguments[2]->kind != NodeKind::ArrowFunction) {
        notImplemented(
            c, "parallel.reduce outside the (range, init, (acc, i) => body) form");
        return "0.0";
    }
    const auto& body = static_cast<const ArrowFunction&>(*c.arguments[2]);
    if (!parallelClosureShape(body, 2)) {
        notImplemented(c, "this parallel.reduce closure shape");
        return "0.0";
    }
    const CppInfo initInfo = info(*c.arguments[1]);
    if (!initInfo.representable || !isPrimitiveShape(initInfo.shape)) {
        notImplemented(c, "this parallel.reduce initializer type");
        return "0.0";
    }
    std::string begin;
    std::string end;
    if (!parallelRange(*c.arguments[0], begin, end)) {
        notImplemented(c, "parallel.reduce over a non-range argument");
        return "0.0";
    }
    const std::string init = expr(*c.arguments[1]);
    const std::string closure = emitParallelClosure(
        body, {initInfo.type, Type::number()}, {initInfo.cpp, "double"});
    std::string out = "::cp3::runtime::parallelReduce(" + begin + ", " + end +
                      ", " + init + ", " + closure;
    if (auto combine = reduceCombine(body, *c.arguments[1])) {
        out += ", " + *combine;
    }
    return out + ")";
}

std::string Generator::emitParallelRun(const CallExpr& c) {
    if (c.arguments.empty()) {
        notImplemented(c, "parallel.run() with no callables");
        return "0.0";
    }
    std::vector<std::string> closures;
    closures.reserve(c.arguments.size());
    for (const auto& arg : c.arguments) {
        if (!arg || arg->kind != NodeKind::ArrowFunction) {
            notImplemented(c, "parallel.run outside the (f1, ..., fN) form");
            return "0.0";
        }
        const auto& body = static_cast<const ArrowFunction&>(*arg);
        if (!parallelClosureShape(body, 0)) {
            notImplemented(c, "this parallel.run closure shape");
            return "0.0";
        }
        closures.push_back(emitParallelClosure(body, {}, {}));
    }
    // parallel.run yields a homogeneous array: every closure must resolve to
    // the same result type. Mixed returns have no defined element type, so the
    // call reports CP3904 instead of silently taking the first closure's type.
    std::optional<Type> element;
    std::size_t firstClosure = 0;
    for (std::size_t i = 0; i < c.arguments.size(); ++i) {
        const auto& body = static_cast<const ArrowFunction&>(*c.arguments[i]);
        auto it = sema_.functions.find(body.nodeId);
        if (it == sema_.functions.end()) continue;
        Type t = it->second.returnType;
        if (t.isLiteral()) t = t.literalBaseType();
        if (t.isUnknown() || t.isError() || t.isVoid()) continue;
        if (!element) {
            element = t;
            firstClosure = i + 1;
            continue;
        }
        if (t == *element) continue;
        if (intMode_) intMode_->failed = true;
        if (!suppressDiagnostics_) {
            sink_.error(diagcode::kParallelRunMixedResultTypes, c.span,
                        "parallel.run() closures must all return the same type "
                        "(closure " + std::to_string(firstClosure) +
                        " returns '" + parallelTypeLabel(*element) +
                        "', closure " + std::to_string(i + 1) + " returns '" +
                        parallelTypeLabel(t) +
                        "'); the result is a homogeneous array, so make every "
                        "closure return one type");
        }
        break;
    }
    return "::cp3::runtime::parallelRun(" + joinStrings(closures, ", ") + ")";
}

std::string Generator::emitParallelCall(const CallExpr& c, const MemberExpr& m) {
    needsParallel_ = true;
    if (m.property == "for") return emitParallelFor(c);
    if (m.property == "reduce") return emitParallelReduce(c);
    if (m.property == "run") return emitParallelRun(c);
    notImplemented(c, "parallel." + m.property);
    return "0.0";
}

}  // namespace cp3
