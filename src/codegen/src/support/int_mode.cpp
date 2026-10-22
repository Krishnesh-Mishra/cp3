// =============================================================================
// src/codegen/src/support/int_mode.cpp - the int64 fast path for `number` (D64).
//
// cp3 `number` is a double, so an integral hot loop pays for double-domain
// semantics (`%` is a libm call, and the divisions the hardware cannot do in
// integer registers). This unit lowers a whole function to a guarded
// std::int64_t body when that is provably observationally identical:
//
//   * `intEligible` accepts a non-async function returning `number` whose
//     parameters are all plain `number` values (no rest/ref/optional/default).
//   * `intBody` emits the body once with `intMode_` set: every node whose type
//     is `number` lowers to a std::int64_t expression built from the guarded
//     helpers in cp3/core/arith.hpp (`iadd`, `imulConst`, `idiv`, ...). Any node the
//     fast path cannot lower exactly calls `intFail()` instead of emitting
//     something unsound, so the attempt is discarded. Proof equals emission:
//     there is no separate analysis pass that could disagree with the emit.
//   * A discarded attempt leaves the ordinary double body as the only
//     lowering; a kept one is emitted next to it as `cp3_i64_<name>`.
//   * The public function then guards its arguments ONCE (`toInt64Exact`),
//     calls the fast body, and falls through to the ordinary double body when
//     a guard rejects an argument or when a guarded operation bails (a thrown
//     `arith::IntBail`, caught around that one call).
//
// The invariant that makes the two bodies agree is `|v| <= 2^53` for every
// value in the region: each int64 value is then exactly the double of the same
// magnitude, so each operation is exactly the double operation, and every
// operation that would leave the invariant bails instead of returning a value.
// A bail is never observable: the wrapper re-runs the double body from the
// start, and the fast body has no side effects (no call, no throw, no yield,
// so nothing can have been observed before the bail). The full correctness
// argument lives in cp3/core/arith.hpp.
//
// Guard placement: integrality and range of the *arguments* are checked once
// per call by the wrapper (loop-invariant: never re-tested per iteration), and
// the per-operation checks only keep the invariant (a range test on the result
// of an addition, `b != 0` and divisibility for `/` and `%`), which is what
// lets the loop itself run on the int64 ALU.
//
// `%` lowering (item 2 of D64) shares this file: `modCall` is the double-domain
// `a % b`, used by functions that are not int64 regions too.
// =============================================================================
#include "detail/generator.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "detail/cpp_writer.h"
#include "detail/int_reads.h"
#include "detail/mangle.h"
#include "detail/int_div_facts.h"
#include "detail/type_map.h"

namespace cp3 {

using namespace detail;

namespace {

constexpr const char* kArith = "::cp3::runtime::arith::";

// A placeholder for a failed attempt: the text is thrown away with it.
constexpr const char* kNoValue = "std::int64_t(0)";

std::string arith(const char* fn) { return std::string(kArith) + fn; }

}  // namespace

// -----------------------------------------------------------------------------
// State + predicates
// -----------------------------------------------------------------------------

bool Generator::intOn() const { return intMode_ != nullptr && !intMode_->failed; }

void Generator::intFail() {
    if (intMode_) intMode_->failed = true;
}

bool Generator::intLocal(const std::string& name) const {
    const auto binding = lookup(name);
    return binding && !binding->global && !binding->cell;
}

bool Generator::numberTyped(const Expr& e) const {
    return classify(effectiveType(e)).shape == Shape::Number;
}

bool Generator::intBinding(const CppInfo& ci) const {
    return intOn() && ci.shape == Shape::Number;
}

// The C++ type source of a local binding: the declared/inferred type, else the
// initializer's (the same rule the emitter has always used).
CppInfo Generator::bindingCpp(const VariableDeclarator& d) const {
    const std::optional<Type> declared = resolvedBindingType(d);
    CppInfo ci = declared ? classify(*declared) : CppInfo{};
    if (!ci.representable && d.init) {
        const CppInfo fromInit = info(*d.init);
        if (fromInit.representable) ci = fromInit;
    }
    return ci;
}

void Generator::declareBinding(const VariableDeclarator& d) {
    if (!d.id || d.id->kind != NodeKind::Identifier) {
        // A destructuring binding is not modelled in the region: the names it
        // binds would be unknown (or stale) to the fast body.
        if (intOn()) intFail();
        return;
    }
    const std::string& name = static_cast<const Identifier&>(*d.id).name;
    intNewBinding(name);
    const bool cell = isCell(name);
    // A binding the fast path lowers to int64 is a `number` in the region, and
    // it is recorded as one so the type queries agree with the C++ type.
    if (intBinding(bindingCpp(d))) {
        declare(name, Type::number(), cell);
    } else {
        declare(name, resolvedBindingType(d), cell);
    }
}

void Generator::intNewBinding(const std::string& name) {
    // The deferred marks are keyed by name, so a shadowing declaration would
    // make them lie about the inner binding: fail the attempt instead.
    if (intMode_ && intMode_->deferred.count(name)) intFail();
    // A divisibility fact is about the old binding, never the new one. Drop it
    // when the name is rebound (a block shadow or a `var` redeclaration).
    detail::dropDivisibleFacts(intMode_.get(), name);
}

bool Generator::intDeferable(const std::string& name) const {
    // A name with a read outside a `return` is observed in the ordinary
    // double domain, so it must stay inside the invariant. A walk that met an
    // unmodelled construct returns `nullopt`: nothing is deferred then.
    return intMode_ != nullptr && intMode_->readOutsideReturns &&
           intMode_->readOutsideReturns->count(name) == 0;
}

// -----------------------------------------------------------------------------
// `%` lowering (the double-domain operator, D64 item 2)
// -----------------------------------------------------------------------------

std::string Generator::modCall(const std::string& lhs, const Expr& rhs,
                               const std::string& rhsText) {
    needsArith_ = true;
    // `x % <integral literal>` compiles to an integer remainder instead of a
    // libm call. The divisor 0 keeps the `std::fmod` path below (JS NaN).
    if (auto k = integralLiteralValue(rhs); k && *k != 0) {
        return arith("modConst<") + std::to_string(*k) + "LL>(" + lhs + ")";
    }
    return arith("mod(") + lhs + ", " + rhsText + ")";
}

// -----------------------------------------------------------------------------
// The int64 emitters (only reached while an attempt is in flight)
// -----------------------------------------------------------------------------

// Every emitter below returns C++ text whose type is std::int64_t and that is
// observationally identical to the double expression it replaces, or calls
// `intFail()` and returns a placeholder (the attempt is discarded either way).

std::string Generator::intLiteral(const Expr& e) {
    if (auto v = integralLiteralValue(e)) {
        return "std::int64_t(" + std::to_string(*v) + ")";
    }
    // A fractional or out-of-range literal has no exact int64 value.
    intFail();
    return kNoValue;
}

std::string Generator::intIdentifier(const Identifier& id) {
    // A name with no plain frame-local binding is a module global, a shared
    // closure cell, `NaN`, `Infinity` or a hoisted read: all of them are
    // doubles (or cells), never int64.
    if (!intLocal(id.name)) {
        intFail();
        return kNoValue;
    }
    if (intMode_ && intMode_->deferred.erase(id.name) > 0) {
        // The bound check of this value was deferred (see intDeferable): this
        // read is where it becomes observable again, so restore the invariant
        // here. A comma expression keeps the text usable as an operand.
        return "(" + arith("pass(") + mapIdent(id.name) + "), " + mapIdent(id.name) + ")";
    }
    return mapIdent(id.name);
}

std::string Generator::intNumberExpr(const Expr& e) {
    switch (e.kind) {
        case NodeKind::NumberLiteral:
            return intLiteral(e);
        case NodeKind::Identifier:
            return intIdentifier(static_cast<const Identifier&>(e));
        case NodeKind::BinaryExpr:
            return intBinary(static_cast<const BinaryExpr&>(e));
        case NodeKind::UnaryExpr:
            return intUnary(static_cast<const UnaryExpr&>(e));
        case NodeKind::UpdateExpr:
            return intUpdate(static_cast<const UpdateExpr&>(e));
        case NodeKind::AssignmentExpr:
            return intAssignment(static_cast<const AssignmentExpr&>(e));
        case NodeKind::ConditionalExpr:
            return intConditional(static_cast<const ConditionalExpr&>(e));
        case NodeKind::LogicalExpr:
            return intLogical(static_cast<const LogicalExpr&>(e));
        default:
            // Calls, members, indexes, `await`, objects/arrays, `this`: none
            // of them is provably in the region.
            intFail();
            return kNoValue;
    }
}


std::string Generator::intBinary(const BinaryExpr& b) {
    if (!numberTyped(*b.left) || !numberTyped(*b.right)) {
        // `number op non-number`: sema rejects most of these, and the rest (an
        // `unknown` operand) cannot be proven exact. A string `+` never gets
        // here: the dispatcher routes by the node's own type, which is then
        // `string`.
        intFail();
        return kNoValue;
    }
    const std::string lhs = expr(*b.left);
    const std::string rhs = expr(*b.right);
    switch (b.op) {
        case BinaryOp::Add:
            return arith("iadd(") + lhs + ", " + rhs + ")";
        case BinaryOp::Subtract:
            return arith("isub(") + lhs + ", " + rhs + ")";
        case BinaryOp::Multiply:
            return intProduct(b);
        case BinaryOp::Divide:
            // A guard `name % k == 0` in force proves the idiv remainder check
            // dead; k > 0 and name in range make the quotient exact. A power of
            // two is an arithmetic right shift: C++20 rounds signed `>>` toward
            // negative infinity, which is exact here.
            if (const auto k = detail::divisibleModulus(b, intMode_.get())) {
                if ((*k & (*k - 1)) == 0 && *k <= (std::int64_t{1} << 62)) {
                    int shift = 0;
                    for (std::int64_t v = *k; v > 1; v >>= 1) ++shift;
                    return "(" + lhs + " >> " + std::to_string(shift) + ")";
                }
                return "(" + lhs + " / " + rhs + ")";
            }
            return arith("idiv(") + lhs + ", " + rhs + ")";
        case BinaryOp::Modulo:
            return arith("imod(") + lhs + ", " + rhs + ")";
        default:
            intFail();
            return kNoValue;
    }
}

// `a * b`: a compile-time factor turns the guard into a constant comparison
// (`imulConst<K>`) and the product into a shift/lea.
std::string Generator::intUnary(const UnaryExpr& u) {
    if (!u.operand || !numberTyped(*u.operand)) {
        intFail();
        return kNoValue;
    }
    const std::string v = expr(*u.operand);
    switch (u.op) {
        case UnaryOp::Negate:
            // `ineg(0)` bails: the double result would be a negative zero.
            return arith("ineg(") + v + ")";
        case UnaryOp::Plus:
            return v;
        default:
            intFail();
            return kNoValue;
    }
}

std::string Generator::intAssignment(const AssignmentExpr& a) {
    if (!a.target || a.target->kind != NodeKind::Identifier || !a.value ||
        !numberTyped(*a.target) || !numberTyped(*a.value)) {
        intFail();
        return kNoValue;
    }
    const auto& id = static_cast<const Identifier&>(*a.target);
    // A local binding only: a module global or shared cell stays a double, so
    // assigning int64 text to it would not compile, and a write to shared state
    // before a bail would be repeated by the double body.
    if (!intLocal(id.name)) {
        intFail();
        return kNoValue;
    }
    return intAssignText(a, id);
}

// The value of a deferred assignment: a `+`/`-` with a small integral literal,
// the one shape whose unchecked result cannot overflow int64 in any runnable
// time. Each raw store moves the value by at most 512 (a raw operand is either
// a deferred value, whose magnitude is inherited, or an in-range expression),
// so an overflow needs on the order of 2^54 raw stores. Returns an empty string
// for every other shape, and the caller then uses the checked lowering.
std::string Generator::intAssignText(const AssignmentExpr& a, const Identifier& id) {
    const std::string bare = mapIdent(id.name);
    if (a.op == AssignOp::Assign) {
        // Only a statement's own expression may defer: its value is discarded,
        // so the raw result is never observed. The operands are emitted with
        // the flag cleared, so a nested assignment stays checked (its value
        // would flow into this one).
        const bool mayDefer = intMode_ && intMode_->stmtExpr && intDeferable(id.name);
        intMode_->stmtExpr = false;
        const std::string raw = mayDefer ? intAssignValue(*a.value) : std::string();
        if (!raw.empty()) {
            detail::dropDivisibleFacts(intMode_.get(), id.name);
            intMode_->deferred.insert(id.name);
            return "(" + bare + " = " + raw + ")";
        }
        intMode_->deferred.erase(id.name);
        const std::string value = expr(*a.value);
        detail::dropDivisibleFacts(intMode_.get(), id.name);
        return "(" + bare + " = " + value + ")";
    }
    intMode_->stmtExpr = false;
    // A read-modify-write needs the old value in range before the operation.
    const std::string value = expr(*a.value);
    const std::string read = expr(*a.target);
    intMode_->deferred.erase(id.name);
    detail::dropDivisibleFacts(intMode_.get(), id.name);
    switch (a.op) {
        case AssignOp::Add:
            return "(" + bare + " = " + arith("iadd(") + read + ", " + value + "))";
        case AssignOp::Subtract:
            return "(" + bare + " = " + arith("isub(") + read + ", " + value + "))";
        case AssignOp::Multiply:
            return "(" + bare + " = " + arith("imul(") + read + ", " + value + "))";
        case AssignOp::Divide:
            return "(" + bare + " = " + arith("idiv(") + read + ", " + value + "))";
        case AssignOp::Modulo:
            return "(" + bare + " = " + arith("imod(") + read + ", " + value + "))";
        default:
            intFail();
            return kNoValue;
    }
}

std::string Generator::intConditional(const ConditionalExpr& c) {
    if (!c.whenTrue || !c.whenFalse || !numberTyped(*c.whenTrue) ||
        !numberTyped(*c.whenFalse)) {
        intFail();
        return kNoValue;
    }
    const std::string test = c.condition ? cond(*c.condition) : "false";
    return "(" + test + " ? " + expr(*c.whenTrue) + " : " + expr(*c.whenFalse) + ")";
}

std::string Generator::intLogical(const LogicalExpr& l) {
    if (!l.left || !l.right || !numberTyped(*l.left) || !numberTyped(*l.right)) {
        intFail();
        return kNoValue;
    }
    // The ordinary lowering keeps whatever C++ type its operands have, so with
    // both operands in the region it stays int64 end to end (`cp3_truthy` and
    // `cp3_ts` have int64 overloads in the generated helper block).
    return emitLogical(l);
}

// -----------------------------------------------------------------------------
// The function-level orchestration (driven by addFunction)
// -----------------------------------------------------------------------------

bool Generator::intEligible(const FunctionDeclaration& fd, const std::string& ret) const {
    if (fd.isAsync || !fd.body || ret != "double") return false;
    // The fast path models no arrow, destructuring pattern, for-of loop or
    // object literal, so a body containing any of them bails out here.
    if (detail::bodyHasClosureOrPattern(*fd.body)) return false;
    // A captured-and-reassigned local of this frame lowers to the shared
    // `Cell<T>` representation, which the fast path does not model either.
    const auto cells = captures_.frameCells.find(fd.nodeId);
    if (cells != captures_.frameCells.end() && !cells->second.empty()) {
        return false;
    }
    for (std::size_t i = 0; i < fd.params.size(); ++i) {
        if (!fd.params[i]) return false;
        const Parameter& p = *fd.params[i];
        if (p.isRest || p.isRef || p.optional || p.defaultValue) return false;
        if (!p.name || p.name->kind != NodeKind::Identifier) return false;
        const std::optional<Type> pt = parameterType(p, fd, i);
        if (!pt || classify(*pt).shape != Shape::Number) return false;
    }
    return true;
}

// Emits the fast body once, with diagnostics suppressed and every fresh-name
// counter restored: a discarded attempt must leave no trace at all.
std::string Generator::intBody(const FunctionDeclaration& fd, bool& kept) {
    intMode_ = std::make_unique<IntMode>();
    if (fd.body) intMode_->readOutsideReturns = namesReadOutsideReturns(*fd.body);
    suppressDiagnostics_ = true;
    const std::string savedReturn = currentReturn_;
    const int savedCounter = uniqueCounter_;
    currentReturn_ = "std::int64_t";
    ++nestingDepth_;
    std::string body;
    pushScope();
    for (std::size_t i = 0; i < fd.params.size(); ++i) {
        const Parameter& p = *fd.params[i];
        if (p.name && p.name->kind == NodeKind::Identifier) {
            declare(static_cast<const Identifier&>(*p.name).name, parameterType(p, fd, i));
        }
    }
    if (fd.body) stmt(*fd.body, body, 1);
    popScope();
    --nestingDepth_;
    currentReturn_ = savedReturn;
    uniqueCounter_ = savedCounter;
    suppressDiagnostics_ = false;
    kept = intOn();
    if (kept) needsArith_ = true;
    intMode_.reset();
    return body;
}

std::string Generator::intSignature(const FunctionDeclaration& fd) const {
    std::vector<std::string> params;
    for (const auto& p : fd.params) {
        const auto& id = static_cast<const Identifier&>(*p->name);
        params.push_back("std::int64_t " + mapIdent(id.name));
    }
    return "CP3_NOINLINE std::int64_t cp3_i64_" + mapIdent(fd.name) + "(" +
           joinStrings(params, ", ") + ")";
}

// The wrapper prologue: one integrality/range guard per argument, then the
// fast call inside a try/catch. The guard runs once per call, never inside the
// loop the fast body may contain; a `catch` means the fast body bailed, and
// control falls through to the ordinary double body that follows.
std::string Generator::intWrapperPrologue(const FunctionDeclaration& fd) const {
    const std::string callee = "cp3_i64_" + mapIdent(fd.name);
    std::vector<std::string> guards;
    std::vector<std::string> args;
    std::string locals;
    for (std::size_t i = 0; i < fd.params.size(); ++i) {
        const auto& id = static_cast<const Identifier&>(*fd.params[i]->name);
        const std::string local = "cp3_i64_arg" + std::to_string(i);
        line(locals, 2, "std::int64_t " + local + " = 0;");
        guards.push_back(arith("toInt64Exact(") + mapIdent(id.name) + ", " + local + ")");
        args.push_back(local);
    }
    const std::string call = callee + "(" + joinStrings(args, ", ") + ")";
    std::string out;
    line(out, 1, "{");
    out += locals;
    int indent = 2;
    if (!guards.empty()) {
        line(out, indent, "if (" + joinStrings(guards, " && ") + ") {");
        ++indent;
    }
    line(out, indent, "try {");
    line(out, indent + 1, "return static_cast<double>(" + call + ");");
    line(out, indent, "} catch (const " + arith("IntBail&) {"));
    line(out, indent, "}");
    if (!guards.empty()) line(out, indent - 1, "}");
    line(out, 1, "}");
    return out;
}

}  // namespace cp3
