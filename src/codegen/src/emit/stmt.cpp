// =============================================================================
// src/codegen/src/emit/stmt.cpp - codegen statement lowering (STYLE.md §8).
//
// Statements, blocks and control flow: variable declarations (including the
// M1 type spelling / nullish folding), if/while/do/for, return (with the
// void-vs-valued signature check), break/continue. Out-of-scope statements
// report CP3002 and emit a compilable stub.
// =============================================================================
#include "detail/generator.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "detail/cpp_writer.h"
#include "detail/mangle.h"
#include "detail/pattern_emitter.h"
#include "detail/type_map.h"
#include "detail/worker.h"

namespace cp3 {

using namespace detail;

void Generator::stmt(const Stmt& s, std::string& out, int indent) {
    // One source-mapping directive per statement (--debug only).
    emitLineDirective(s, out);
    // Loops, switch, labels and break/continue carry generated labels and flow
    // frames; their lowering unit runs before the ordinary arm table.
    if (emitControlDispatch(s, out, indent)) return;
    switch (s.kind) {
        case NodeKind::BlockStatement: {
            pushScope();
            line(out, indent, "{");
            const auto& block = static_cast<const BlockStatement&>(s);
            for (std::size_t i = 0; i < block.body.size(); ++i) {
                const Stmt* child = block.body[i].get();
                if (!child) continue;
                // The int64 fast path owns the statements it rewrote; it
                // marks them so the ordinary walker skips them.
                if (int64SkipStmts_.count(child->nodeId) != 0) continue;
                // The fused map update replaces the pair it consumes, so it
                // must not start on a statement the int64 path owns.
                if (emitFusedMapUpdate(block.body, i, out, indent + 1)) continue;
                stmt(*child, out, indent + 1);
            }
            line(out, indent, "}");
            popScope();
            return;
        }
        case NodeKind::EmptyStatement:
            line(out, indent, ";");
            return;
        case NodeKind::ExpressionStatement: {
            const auto& es = static_cast<const ExpressionStatement&>(s);
            if (!es.expression) {
                line(out, indent, ";");
                return;
            }
            // A statement's expression may defer its bound check (intAssignText).
            const bool wasStmt = intOn() && intMode_->stmtExpr;
            if (intOn()) intMode_->stmtExpr = true;
            const std::string text = expr(*es.expression);
            if (intOn()) intMode_->stmtExpr = wasStmt;
            line(out, indent, text + ";");
            return;
        }
        case NodeKind::VariableDeclaration:
            emitVarDecl(static_cast<const VariableDeclaration&>(s), out, indent);
            return;
        case NodeKind::IfStatement:
            emitIf(static_cast<const IfStatement&>(s), out, indent);
            return;
        case NodeKind::ReturnStatement:
            emitReturn(static_cast<const ReturnStatement&>(s), out, indent);
            return;
        case NodeKind::ThrowStatement:
            emitThrow(static_cast<const ThrowStatement&>(s), out, indent);
            return;
        case NodeKind::TryStatement:
            emitTry(static_cast<const TryStatement&>(s), out, indent);
            return;
        case NodeKind::ClassDeclaration:
            // A function-local class (spec 15) emits its struct inline: a
            // local type cannot have out-of-line definitions at namespace scope.
            emitLocalClass(static_cast<const ClassDeclaration&>(s), out, indent);
            return;
        case NodeKind::EnumDeclaration:
            // A function-local enum (spec 04): its `enum class` is declared at
            // namespace scope, so the statement itself emits nothing.
            emitLocalEnum(s);
            return;
        default:
            notImplemented(s, "this statement");
            line(out, indent, "/* CP3002: unsupported statement */");
            return;
    }
}

void Generator::emitBranch(const Stmt& s, std::string& out, int indent) {
    if (s.kind == NodeKind::BlockStatement) {
        const auto& block = static_cast<const BlockStatement&>(s);
        for (std::size_t i = 0; i < block.body.size(); ++i) {
            const Stmt* child = block.body[i].get();
            if (!child) continue;
            if (int64SkipStmts_.count(child->nodeId) != 0) continue;
            if (emitFusedMapUpdate(block.body, i, out, indent)) continue;
            stmt(*child, out, indent);
        }
    } else {
        stmt(s, out, indent);
    }
}

void Generator::emitWhile(const WhileStatement& ws, std::string& out,
                          int indent) {
    const std::string condition = ws.condition ? cond(*ws.condition) : "false";
    line(out, indent, "while (" + condition + ") {");
    if (ws.body) emitBranch(*ws.body, out, indent + 1);
    line(out, indent, "}");
}

void Generator::emitDoWhile(const DoWhileStatement& dw, std::string& out,
                            int indent) {
    line(out, indent, "do {");
    if (dw.body) emitBranch(*dw.body, out, indent + 1);
    const std::string condition = dw.condition ? cond(*dw.condition) : "false";
    line(out, indent, "} while (" + condition + ");");
}

void Generator::emitIf(const IfStatement& is, std::string& out, int indent) {
    // `x % k == 0` in the then-branch proves divisibility, so the division
    // inside it needs no remainder check (intBinary). The fact is scoped to the
    // then-branch and invalidated by any write to `x`.
    std::optional<std::pair<std::string, std::int64_t>> fact;
    if (intOn() && is.condition) fact = divisibleGuard(*is.condition);
    // The fact is a claim about every later point in the branch: only a
    // straight-line branch that cannot defer, repeat or outsource work may use
    // it.
    if (fact && is.thenBranch && !factStableBranch(*is.thenBranch)) {
        fact.reset();
    }
    const std::string condition = is.condition ? cond(*is.condition) : "false";
    std::size_t factDepth = 0;
    if (fact) {
        factDepth = intMode_->divisible.size();
        intMode_->divisible.push_back(
            IntMode::DivisibleFact{fact->first, fact->second, true});
    }
    line(out, indent, "if (" + condition + ") {");
    if (is.thenBranch) emitBranch(*is.thenBranch, out, indent + 1);
    // A write inside the branch may have invalidated the fact in place, so
    // never grow past the entry depth and drop any entry the branch left.
    if (fact && intMode_->divisible.size() > factDepth) {
        intMode_->divisible.resize(factDepth);
    }
    if (is.elseBranch) {
        line(out, indent, "} else {");
        emitBranch(*is.elseBranch, out, indent + 1);
    }
    line(out, indent, "}");
}

void Generator::emitVarDecl(const VariableDeclaration& vd, std::string& out, int indent) {
    for (const auto& decl : vd.declarations) {
        if (!decl) continue;
        // `let [a, b] = ...` / `let { a } = ...` unpacks in its own unit.
        if (emitDestructureDecl(*decl, vd.kind, out, indent)) continue;
        std::string text = declaratorText(*decl, vd.kind);
        line(out, indent, text + ";");
        declareBinding(*decl);
    }
}

std::string Generator::declaratorText(const VariableDeclarator& d, VarKind kind) {
    if (!d.id || d.id->kind != NodeKind::Identifier) {
        if (d.id) notImplemented(*d.id, "destructuring binding");
        return "auto cp3_error = 0.0";
    }
    const std::string name = mapIdent(static_cast<const Identifier&>(*d.id).name);
    // A proven entry-body int64 binding declares its int64 spelling; the
    // name-keyed reads convert back (support/top_int64.cpp).
    if (const std::string text = int64DeclaratorText(d); !text.empty()) {
        return text;
    }
    // A proven-safe index read binds the element by const reference. The
    // int64 fast path owns the int64-mode bindings, so the two never
    // compete for the same declarator.
    if (!intOn()) {
        const std::string ref = arrayRefDeclText(d, name);
        if (!ref.empty()) return ref;
    }
    const std::string cv = (kind == VarKind::Const) ? "const " : "";
    // A declared type M1 cannot represent (`number | string`, an object /
    // array / class type, ...) is out of scope: report it so the unit is
    // treated as not-implemented instead of being quietly mis-typed.
    if (d.type) {
        if (auto dt = typeOfNode(*d.type)) {
            if (!classify(*dt).representable && !dt->isUnknown() &&
                !dt->isError() && !isChannelHandleType(*dt)) {
                notImplemented(d, "this declared type");
            }
        }
    }
    const CppInfo ci = bindingCpp(d);
    // A worker handle (D80) is a movable, owning runtime value: `const auto`
    // would make its members unusable (`postMessage` is non-const), so the
    // binding form drops the cp3 `const` and lets C++ deduce the handle type.
    if (d.init && isChannelHandleType(ci.type)) {
        return "auto " + name + " = " + expr(*d.init);
    }
    // Inside an int64 attempt a `number` binding is a std::int64_t, and its
    // initializer must therefore be a region value: a `number` in C++ that the
    // fast path cannot account for would make the two bodies disagree.
    const bool asInt = intBinding(ci);
    // A captured-and-reassigned local of this frame is the shared-cell
    // representation: reads and writes go through `get`/`set` so the enclosing
    // frame and every closure share one binding.
    const bool cell = isCell(static_cast<const Identifier&>(*d.id).name);
    const std::string valueCpp =
        (ci.representable && ci.shape != Shape::Void) ? ci.cpp : "double";
    const std::string typeSpelling =
        cell ? "::cp3::runtime::Cell<" + valueCpp + ">"
             : (asInt ? "std::int64_t" : (ci.representable ? ci.cpp : "auto"));
    if (d.init) {
        if (asInt && !numberTyped(*d.init)) intFail();
        std::string initText;
        if (ci.shape == Shape::Optional && isNullishLiteral(*d.init)) {
            initText = "std::nullopt";
        } else if (std::string special = modelInitText(d, ci.type);
                   !special.empty()) {
            // A class-typed binding built from an object literal (spec 15).
            initText = special;
        } else if (d.type && ci.shape == Shape::Object &&
                   d.init->kind == NodeKind::ObjectLiteral) {
            // An explicitly annotated record fixes the struct type; build that
            // struct from the literal (a field may be a union/option, whose
            // struct differs from the literal's narrower shape).
            initText = objectLiteralForType(
                static_cast<const ObjectLiteral&>(*d.init), ci.type);
            if (initText.empty()) initText = expr(*d.init);
        } else if (std::string special = declaratorInitText(d, ci.type);
                   !special.empty()) {
            // `let a: number[] = Array(3)` / `let a: number[] = []` (the
            // annotation fixes the element type;
            // src/std/typed-arrays/codegen.cpp).
            initText = special;
        } else {
            const std::optional<Type> savedExpected = expected_;
            if (!ci.type.isUnknown() && !ci.type.isError()) expected_ = ci.type;
            initText = expr(*d.init);
            expected_ = savedExpected;
        }
        // `Cell`'s constructors are explicit; a cell is never `const` (the
        // closures write through it).
        if (cell) return typeSpelling + " " + name + "(" + initText + ")";
        return cv + typeSpelling + " " + name + " = " + initText;
    }

    if (cell) {
        return typeSpelling + " " + name + "{}";
    }
    if (ci.shape == Shape::Optional || ci.shape == Shape::Maybe) {
        return cv + ci.cpp + " " + name;
    }
    if (ci.representable && ci.shape != Shape::Void) {
        return cv + typeSpelling + " " + name + "{}";
    }
    return cv + "auto " + name + " = cp3::runtime::undefined";
}

void Generator::emitReturn(const ReturnStatement& r, std::string& out, int indent) {
    // Inside an `async function` body the generated C++ is a coroutine
    // (spec §14), so cp3 `return` lowers to `co_return`.
    const std::string keyword = inAsyncBody_ ? "co_return" : "return";
    if (r.argument) {
        // A `number` function's fast body returns std::int64_t, so the
        // returned value must be a region value.
        if (intOn() && !numberTyped(*r.argument)) intFail();
        // Always lower the returned expression so its own out-of-scope
        // parts report CP3002 instead of being silently dropped.
        const std::string value =
            currentReturnTuple_ && r.argument->kind == NodeKind::ArrayLiteral
                ? emitTupleLiteral(static_cast<const ArrayLiteral&>(*r.argument))
                : expr(*r.argument);
        if (currentReturn_ == "void") {
            // The enclosing signature is `void` either because the cp3
            // return type is out of M1 scope or the function is genuinely
            // void. Either way a value is being returned, so flag it and
            // keep the emitted C++ valid.
            notImplemented(r, "returning a value from a void/unrepresentable signature");
            line(out, indent, "(void)(" + value + ");");
            line(out, indent, keyword + ";");
            return;
        }
        line(out, indent, keyword + " " + value + ";");
        return;
    }
    if (currentReturn_ == "void") {
        line(out, indent, keyword + ";");
    } else {
        line(out, indent, keyword + " {};");
    }
}

void Generator::emitThrow(const ThrowStatement& t, std::string& out, int indent) {
    // A throw would observe a value (and the fast body must have no side
    // effects before a bail), so it stays in the double body.
    if (intOn()) intFail();
    if (!t.argument) {
        line(out, indent, "throw;");
        return;
    }
    // `throw e;` where `e` is a live `catch (e)` binding re-throws the value
    // being handled (cp3/async/promise.hpp's `Thrown` is handle-like, so the plain
    // C++ rethrow is exactly the cp3 semantics).
    if (t.argument->kind == NodeKind::Identifier) {
        const std::string& name = static_cast<const Identifier&>(*t.argument).name;
        for (const std::string& bound : thrownBindings_) {
            if (bound == name) {
                line(out, indent, "throw;");
                return;
            }
        }
    }
    line(out, indent,
         "throw ::cp3::runtime::Thrown::make(" + expr(*t.argument) + ");");
}

void Generator::emitTry(const TryStatement& ts, std::string& out, int indent) {
    // A `catch` binding is a boxed value and a `finally` guard may run while a
    // bail is in flight: both are observed side effects, so try stays in the
    // double body.
    if (intOn()) intFail();
    const bool hasCatch = ts.handler != nullptr;
    const bool hasFinally = ts.finallyBlock != nullptr;

    std::string guard;
    if (hasFinally) {
        // `finally` must run on every exit - normal completion, `break`,
        // `continue`, `return`, or a propagating throw - exactly once
        // (spec §07, DECISIONS D37). An RAII guard gives all of that for free;
        // the explicit `runNow()` below covers the normal path.
        guard = "cp3_fin" + std::to_string(uniqueCounter_++);
        line(out, indent, "{");
        ++indent;
        line(out, indent, "cp3_finally_guard " + guard + "([&]() {");
        emitBranch(*ts.finallyBlock, out, indent + 1);
        line(out, indent, "});");
    }

    if (hasCatch) {
        line(out, indent, "try {");
        if (ts.tryBlock) emitBranch(*ts.tryBlock, out, indent + 1);
        const CatchClause& cc = *ts.handler;
        line(out, indent, "} catch (...) {");
        // Box whatever was thrown (`Thrown` values pass through unchanged),
        // so a cp3 `catch (e)` binds any value (DECISIONS D39).
        bool bound = cc.param && cc.param->kind == NodeKind::Identifier;
        std::string boundName;
        if (bound) {
            boundName = static_cast<const Identifier&>(*cc.param).name;
            line(out, indent + 1,
                 "auto " + mapIdent(boundName) +
                     " = ::cp3::runtime::Thrown::fromCurrentException();");
            thrownBindings_.push_back(boundName);
        }
        if (cc.body) emitBranch(*cc.body, out, indent + 1);
        if (bound) thrownBindings_.pop_back();
        line(out, indent, "}");
    } else if (ts.tryBlock) {
        // `try { } finally { }` (no handler): the guard's destructor runs the
        // finally block if the body throws; nothing else is needed here.
        emitBranch(*ts.tryBlock, out, indent);
    }

    if (hasFinally) {
        line(out, indent, guard + ".runNow();");
        --indent;
        line(out, indent, "}");
    }
}

}  // namespace cp3
