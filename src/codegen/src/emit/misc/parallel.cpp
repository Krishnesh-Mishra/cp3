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
// type for a reduce) before lowering the arrow itself. A reduce body that is one
// of the associative shapes (`acc + <expr>` from 0 where the expression never
// reads the accumulator, `acc * <expr>` from 1, the max/min conditional) gets
// the chunked 5-argument lowering with a synthesized combine op, but only when
// support/parallel_combine.cpp proves the expression pure and integer exact;
// every other reduce keeps the sequential 4-argument form, which is still
// exact.
//
// The surface itself is closed (spec 13): `parallel` has exactly the members
// `for`, `reduce` and `run`, and each takes the argument list above. A call
// outside that surface is a program error, not a feature the compiler is still
// missing, so it is reported as one - a member miss (CP3305), an arity
// mismatch (CP3403) or a type mismatch (CP3401) - instead of skipping the
// program.
// =============================================================================
#include "detail/generator/generator.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "cp3/diag/diagnostic_codes.hpp"
#include "detail/generator/cpp_writer.h"
#include "detail/parallel/parallel.h"
#include "detail/parallel/parallel_combine.h"
#include "detail/generator/type_map.h"

namespace cp3 {

using namespace detail;

namespace {

// A printable label for a diagnostic message: the nominal label when the type
// carries one (class, enum, type parameter), the kind name otherwise.
std::string parallelTypeLabel(const Type& t) {
    return t.name.empty() ? typeKindName(t.kind) : t.name;
}

// The accumulator of a `parallel.reduce` is copied once per element, so it
// must be an ordinary cp3 value type - the same set the worker channel
// carries (D80): a number, string, boolean, array, record, enum, or a
// union/option of those. A live handle, a promise or a function has no
// per-element copy, so it is not an accumulator.
bool isAccumulatorShape(Shape s) {
    switch (s) {
        case Shape::Number:
        case Shape::String:
        case Shape::Bool:
        case Shape::Native:
        case Shape::Array:
        case Shape::Object:
        case Shape::Enum:
        case Shape::Union:
        case Shape::Optional:
        case Shape::Maybe:
            return true;
        default:
            return false;
    }
}

// Why a `parallel.*` closure cannot be lowered. The runtime hands the closure
// plain numbers (the loop index, and the accumulator for a reduce), so the
// closure has to be a plain synchronous function of exactly those parameters:
// an async closure has no event loop to drive its awaits (D71), and the
// runtime calls the closure with a fixed argument list.
struct ClosureFault {
    bool faulty = false;
    const char* code = diagcode::kTypeMismatch;
    std::string message;
};

// The fault of a closure that does not fit the entry point's shape, or a
// non-faulty value when it does. `count` is the number of parameters the
// runtime hands the closure; `params` names them for the diagnostic.
ClosureFault closureFault(const ArrowFunction& a, std::size_t count,
                           const std::string& entry, const std::string& params) {
    ClosureFault fault;
    if (a.isAsync) {
        fault.faulty = true;
        fault.message =
            "type mismatch: " + entry +
            "'s closure cannot be async: parallel.* is synchronous (the calling "
            "thread blocks until every closure finishes), so there is no event "
            "loop to drive an await";
        return fault;
    }
    if (!a.typeParams.empty()) {
        fault.faulty = true;
        fault.message = "type mismatch: " + entry +
                        "'s closure cannot have type parameters";
        return fault;
    }
    if (a.params.size() != count) {
        fault.faulty = true;
        fault.code = diagcode::kArityMismatch;
        fault.message = "arity mismatch: " + entry + "'s closure must take " +
                        std::to_string(count) +
                        (count == 1 ? " parameter (" : " parameters (") + params +
                        "), got " + std::to_string(a.params.size());
        return fault;
    }
    for (const auto& param : a.params) {
        if (!param || !asIdentifier(param->name.get())) {
            fault.faulty = true;
            fault.message = "type mismatch: " + entry +
                            "'s closure parameters must be plain identifiers";
            return fault;
        }
        if (param->isRest) {
            fault.faulty = true;
            fault.message = "type mismatch: " + entry +
                            "'s closure cannot take a rest parameter";
            return fault;
        }
    }
    return fault;
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
    return !closureFault(a, count, "parallel.*", "the index").faulty;
}

std::string Generator::emitParallelFor(const CallExpr& c) {
    if (c.arguments.size() != 2 || !c.arguments[0] || !c.arguments[1]) {
        reportCodegenError(c, diagcode::kArityMismatch,
                           "arity mismatch: parallel.for takes a range and a "
                           "closure (2 arguments), got " +
                               std::to_string(c.arguments.size()));
        return "0.0";
    }
    if (c.arguments[1]->kind != NodeKind::ArrowFunction) {
        reportCodegenError(c, diagcode::kTypeMismatch,
                           "type mismatch: parallel.for's second argument must "
                           "be a closure: parallel.for(range, (i) => body)");
        return "0.0";
    }
    const auto& body = static_cast<const ArrowFunction&>(*c.arguments[1]);
    const ClosureFault fault =
        closureFault(body, 1, "parallel.for", "the index");
    if (fault.faulty) {
        reportCodegenError(c, fault.code, fault.message);
        return "0.0";
    }
    std::string begin;
    std::string end;
    if (!parallelRange(*c.arguments[0], begin, end)) {
        reportCodegenError(c, diagcode::kTypeMismatch,
                           "type mismatch: parallel.for's first argument must "
                           "be a range: parallel.for(0..n, (i) => body)");
        return "0.0";
    }
    const std::string closure =
        emitParallelClosure(body, {Type::number()}, {"double"});
    return "::cp3::runtime::parallelFor(" + begin + ", " + end + ", " + closure +
           ")";
}

std::string Generator::emitParallelReduce(const CallExpr& c) {
    if (c.arguments.size() != 3 || !c.arguments[0] || !c.arguments[1] ||
        !c.arguments[2]) {
        reportCodegenError(c, diagcode::kArityMismatch,
                           "arity mismatch: parallel.reduce takes a range, an "
                           "initializer and a closure (3 arguments), got " +
                               std::to_string(c.arguments.size()));
        return "0.0";
    }
    if (c.arguments[2]->kind != NodeKind::ArrowFunction) {
        reportCodegenError(c, diagcode::kTypeMismatch,
                           "type mismatch: parallel.reduce's third argument "
                           "must be a closure: parallel.reduce(range, init, "
                           "(acc, i) => body)");
        return "0.0";
    }
    const auto& body = static_cast<const ArrowFunction&>(*c.arguments[2]);
    const ClosureFault fault = closureFault(body, 2, "parallel.reduce",
                                            "the accumulator and the index");
    if (fault.faulty) {
        reportCodegenError(c, fault.code, fault.message);
        return "0.0";
    }
    const CppInfo initInfo = info(*c.arguments[1]);
    if (!initInfo.representable || !isAccumulatorShape(initInfo.shape)) {
        // The accumulator is copied once per element, so it must be an
        // ordinary cp3 value type; a handle, a promise or a function is not
        // one. Sema reports the same rule as CP3913; this is the defensive
        // path for a unit lowered without a sema table.
        reportCodegenError(c, diagcode::kParallelAccumulatorType,
                           "a parallel.reduce accumulator must be a cp3 value "
                           "type (a number, string, boolean, array, record, "
                           "enum, or a union of those), not '" +
                               parallelTypeLabel(initInfo.type) + "'");
        return "0.0";
    }
    std::string begin;
    std::string end;
    if (!parallelRange(*c.arguments[0], begin, end)) {
        reportCodegenError(c, diagcode::kTypeMismatch,
                           "type mismatch: parallel.reduce's first argument "
                           "must be a range: parallel.reduce(0..n, init, "
                           "(acc, i) => body)");
        return "0.0";
    }
    const std::string init = expr(*c.arguments[1]);
    const std::string closure = emitParallelClosure(
        body, {initInfo.type, Type::number()}, {initInfo.cpp, "double"});
    std::string out = "::cp3::runtime::parallelReduce";
    if (auto combine = parallelReduceCombine(body, *c.arguments[1],
                                             functionDecls_, sema_)) {
        out += std::string("<::cp3::runtime::ParallelCombineKind::") +
               combine->kind + ">";
        out += "(" + begin + ", " + end + ", " + init + ", " + closure + ", " +
               combine->lambda + ")";
        return out;
    }
    return out + "(" + begin + ", " + end + ", " + init + ", " + closure +
           ")";
}

std::string Generator::emitParallelRun(const CallExpr& c) {
    if (c.arguments.empty()) {
        reportCodegenError(c, diagcode::kArityMismatch,
                           "arity mismatch: parallel.run takes at least one "
                           "callable, got 0");
        return "0.0";
    }
    std::vector<std::string> closures;
    closures.reserve(c.arguments.size());
    for (const auto& arg : c.arguments) {
        if (!arg || arg->kind != NodeKind::ArrowFunction) {
            reportCodegenError(c, diagcode::kTypeMismatch,
                               "type mismatch: parallel.run's arguments must be "
                               "closures: parallel.run(f1, ..., fN)");
            return "0.0";
        }
        const auto& body = static_cast<const ArrowFunction&>(*arg);
        if (!parallelClosureShape(body, 0)) {
            // The shapes sema rejects as CP3912 (checks/parallel_check.cpp):
            // mirror the same program errors so a codegen-only pass reports
            // them instead of skipping the unit.
            if (body.isAsync) {
                reportCodegenError(
                    c, diagcode::kParallelClosureShape,
                    "a parallel.* closure cannot be async: parallel.* is "
                    "synchronous and its pool threads have no event loop, so an "
                    "await inside the closure would never be driven");
            } else if (!body.typeParams.empty()) {
                reportCodegenError(
                    c, diagcode::kParallelClosureShape,
                    "a parallel.run() closure cannot be generic: the call site "
                    "has no argument to infer its type parameters from");
            } else {
                reportCodegenError(
                    c, diagcode::kParallelClosureShape,
                    "a parallel.run() closure takes no parameters: the call "
                    "runs each closure with no arguments");
            }
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
    // A `parallel.*` member outside the documented surface (spec 13: `for`,
    // `reduce` and `run`) is a program error, not a feature the compiler is
    // still missing: report the member miss.
    reportCodegenError(c, diagcode::kMemberNotFound,
                       "'parallel' has no member '" + m.property +
                           "'; the parallel surface is 'for', 'reduce' and "
                           "'run'");
    return "0.0";
}

}  // namespace cp3
