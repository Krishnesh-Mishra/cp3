// =============================================================================
// src/sema/src/checks/stmt_check.cpp - statement checking, control flow, return
// analysis.
//
// Owns the statement passes: block/scope walk (`checkStmts`), the statement
// dispatch (`visitStmt` + `visitExecStmt` / `visitDeclStmt`), condition
// checking, loop/switch/return/try analysis, `typeof` narrowing, and
// `analyzeFunctionBody` return analysis. The `allPathsReturn` /
// `switchCannotFallThrough` flow helpers live in flow_check.cpp; declarations
// are built in declarations.cpp (STYLE.md §8).
// =============================================================================
#include "detail/analyzer.h"

#include <cstddef>
#include <cstdint>
#include <string>

#include "cp3/diag/diagnostic_codes.hpp"

namespace cp3 {

// =============================================================================
// Block walk + statement dispatch
// =============================================================================
void Analyzer::checkStmts(StmtList& body) {
    // Type declarations are hoisted first so that function signatures and
    // other annotations can refer to types declared later in the block.
    for (auto& s : body) {
        if (s) hoistType(*s);
    }
    for (auto& s : body) {
        if (s && s->kind == NodeKind::FunctionDeclaration) {
            hoistFunction(static_cast<FunctionDeclaration&>(*s));
        }
    }
    // Class member tables resolve before bodies run so `this`, member access
    // and `new` see the full surface (class_check.cpp).
    resolveClasses(body);
    // `var` names are visible (as `undefined`) before their declaration.
    hoistVars(body);
    for (auto& s : body) {
        if (s) visitStmt(*s);
    }
}

void Analyzer::visitStmt(Stmt& s) {
    switch (s.kind) {
        case NodeKind::EmptyStatement:
        case NodeKind::ExpressionStatement:
        case NodeKind::BlockStatement:
        case NodeKind::VariableDeclaration:
        case NodeKind::FunctionDeclaration:
        case NodeKind::IfStatement:
        case NodeKind::WhileStatement:
        case NodeKind::DoWhileStatement:
        case NodeKind::ForStatement:
        case NodeKind::ForOfStatement:
        case NodeKind::ForInStatement:
        case NodeKind::SwitchStatement:
        case NodeKind::BreakStatement:
        case NodeKind::ContinueStatement:
        case NodeKind::ReturnStatement:
        case NodeKind::ThrowStatement:
        case NodeKind::TryStatement:
        case NodeKind::LabeledStatement:
            visitExecStmt(s);
            return;
        case NodeKind::ClassDeclaration:
        case NodeKind::EnumDeclaration:
        case NodeKind::TypeAliasDeclaration:
        case NodeKind::InterfaceDeclaration:
        case NodeKind::ImportDeclaration:
        case NodeKind::ExportDeclaration:
            visitDeclStmt(s);
            return;
        default:
            return;
    }
}

void Analyzer::visitExecStmt(Stmt& s) {
    switch (s.kind) {
        case NodeKind::EmptyStatement:
            return;
        case NodeKind::ExpressionStatement: {
            auto& es = static_cast<ExpressionStatement&>(s);
            if (es.expression) inferExpr(*es.expression);
            return;
        }
        case NodeKind::BlockStatement: {
            auto& bs = static_cast<BlockStatement&>(s);
            pushScope();
            checkStmts(bs.body);
            popScope();
            return;
        }
        case NodeKind::VariableDeclaration:
            declareVariableDeclaration(static_cast<VariableDeclaration&>(s));
            return;
        case NodeKind::FunctionDeclaration: {
            auto& fn = static_cast<FunctionDeclaration&>(s);
            if (declaredFns_.count(fn.nodeId) == 0) hoistFunction(fn);
            analyzeFunctionBody(fn);
            return;
        }
        case NodeKind::IfStatement:
            analyzeIf(static_cast<IfStatement&>(s));
            return;
        case NodeKind::WhileStatement:
            analyzeWhile(static_cast<WhileStatement&>(s));
            return;
        case NodeKind::DoWhileStatement:
            analyzeDoWhile(static_cast<DoWhileStatement&>(s));
            return;
        case NodeKind::ForStatement:
            analyzeFor(static_cast<ForStatement&>(s));
            return;
        case NodeKind::ForOfStatement:
            analyzeForOf(static_cast<ForOfStatement&>(s));
            return;
        case NodeKind::ForInStatement:
            analyzeForIn(static_cast<ForInStatement&>(s));
            return;
        case NodeKind::SwitchStatement:
            analyzeSwitch(static_cast<SwitchStatement&>(s));
            return;
        case NodeKind::BreakStatement:
            visitBreak(static_cast<BreakStatement&>(s));
            return;
        case NodeKind::ContinueStatement:
            visitContinue(static_cast<ContinueStatement&>(s));
            return;
        case NodeKind::ReturnStatement:
            analyzeReturn(static_cast<ReturnStatement&>(s));
            return;
        case NodeKind::ThrowStatement: {
            auto& t = static_cast<ThrowStatement&>(s);
            if (t.argument) {
                const Type thrown = inferExpr(*t.argument);
                if (!thrown.isError() && !isErrorType(thrown)) {
                    report(
                        diagcode::kThrowNonError, t.span,
                        "TypeScript allows throwing any value, but CP3 only "
                        "allows Error objects (Error or a subclass). CP3 "
                        "rejects throwing '" +
                            pretty(thrown) +
                            "' because a catch binds one static Error handle: "
                            "there is no runtime metadata or hidden boxing in "
                            "the native output. Construct an Error (or a "
                            "subclass) and throw that instead.");
                }
            }
            return;
        }
        case NodeKind::TryStatement:
            analyzeTry(static_cast<TryStatement&>(s));
            return;
        case NodeKind::LabeledStatement:
            visitLabeled(static_cast<LabeledStatement&>(s));
            return;
        default:
            return;
    }
}

// =============================================================================
// Control-flow statement helpers (the declaration statements - visitDeclStmt
// and its handlers - live in declarations.cpp).
// =============================================================================

// The control-flow context a function body starts in (see analyzer.h): the
// body sees no loop, no switch and no label of the enclosing function, so a
// `break`/`continue` inside it is judged against its own function only.
Analyzer::FunctionContext Analyzer::enterFunctionBody() {
    FunctionContext saved;
    saved.loop = loopDepth_;
    saved.switchCount = switchDepth_;
    saved.labels = labels_;
    saved.iterationLabels = iterationLabels_;
    loopDepth_ = 0;
    switchDepth_ = 0;
    labels_.clear();
    iterationLabels_.clear();
    return saved;
}

void Analyzer::leaveFunctionBody(const FunctionContext& saved) {
    loopDepth_ = saved.loop;
    switchDepth_ = saved.switchCount;
    labels_ = saved.labels;
    iterationLabels_ = saved.iterationLabels;
}

// A statement a `continue` may target: the iteration statements (the loops).
// A label on a block or a switch may only be broken out of.
static bool isIterationStatement(const Stmt* s) {
    if (s == nullptr) return false;
    switch (s->kind) {
        case NodeKind::WhileStatement:
        case NodeKind::DoWhileStatement:
        case NodeKind::ForStatement:
        case NodeKind::ForOfStatement:
        case NodeKind::ForInStatement:
            return true;
        default:
            return false;
    }
}

void Analyzer::visitBreak(BreakStatement& b) {
    if (b.hasLabel) {
        if (!hasLabel(b.label)) {
            report(diagcode::kUndefinedLabel, b.span,
                   "undefined label '" + b.label + "'");
        }
    } else if (loopDepth_ == 0 && switchDepth_ == 0) {
        report(diagcode::kBreakOutsideLoop, b.span,
               "break outside of a loop or switch");
    }
}

void Analyzer::visitContinue(ContinueStatement& c) {
    if (c.hasLabel) {
        if (!hasLabel(c.label)) {
            report(diagcode::kUndefinedLabel, c.span,
                   "undefined label '" + c.label + "'");
        } else if (!hasIterationLabel(c.label)) {
            // JS: a `continue` needs an iteration statement; a label on a
            // block or a switch may only be broken out of.
            report(diagcode::kContinueNonIterationLabel, c.span,
                   "continue label '" + c.label +
                       "' does not denote an iteration statement");
        }
    } else if (loopDepth_ == 0) {
        report(diagcode::kContinueOutsideLoop, c.span,
               "continue outside of a loop");
    }
}

void Analyzer::visitLabeled(LabeledStatement& l) {
    labels_.push_back(l.label);
    const bool iteration = isIterationStatement(l.body.get());
    if (iteration) iterationLabels_.push_back(l.label);
    if (l.body) visitStmt(*l.body);
    if (iteration) iterationLabels_.pop_back();
    labels_.pop_back();
}

// =============================================================================
// Conditions + labels
// =============================================================================
void Analyzer::checkCondition(const Type& t, const SourceSpan& span) {
    if (t.isVoid()) {
        report(diagcode::kInvalidCondition, span,
               "invalid condition: a void expression is not a valid condition");
    }
}

bool Analyzer::hasLabel(const std::string& label) const {
    for (const std::string& l : labels_) {
        if (l == label) return true;
    }
    return false;
}

// A label that names an iteration statement: the only labels a `continue` may
// target.
bool Analyzer::hasIterationLabel(const std::string& label) const {
    for (const std::string& l : iterationLabels_) {
        if (l == label) return true;
    }
    return false;
}

// =============================================================================
// Control flow
// =============================================================================
void Analyzer::analyzeIf(IfStatement& stmt) {
    std::string narrowName;
    Type narrowType = Type::unknown();
    bool narrowed = false;
    bool negated = false;
    if (stmt.condition) {
        inferExpr(*stmt.condition);
        checkCondition(typeOfExpr(*stmt.condition), stmt.condition->span);
        narrowed =
            detectTypeofNarrowing(*stmt.condition, narrowName, narrowType, negated);
    }
    Binding orig;
    const bool hadOrig = narrowed && lookup(narrowName, orig);
    const Type complement =
        hadOrig ? withoutMember(orig.type, narrowType) : Type::unknown();

    pushScope();
    if (narrowed) {
        overrideBinding(narrowName, negated ? complement : narrowType,
                        stmt.span);
    }
    if (stmt.thenBranch) visitStmt(*stmt.thenBranch);
    popScope();

    if (stmt.elseBranch) {
        pushScope();
        if (narrowed) {
            overrideBinding(narrowName, negated ? narrowType : complement,
                            stmt.span);
        }
        visitStmt(*stmt.elseBranch);
        popScope();
    }
}

bool Analyzer::detectTypeofNarrowing(Expr& cond, std::string& name, Type& type,
                                     bool& negated) {
    if (cond.kind != NodeKind::BinaryExpr) return false;
    auto& be = static_cast<BinaryExpr&>(cond);
    if (be.op != BinaryOp::StrictEqual && be.op != BinaryOp::Equal &&
        be.op != BinaryOp::StrictNotEqual && be.op != BinaryOp::NotEqual) {
        return false;
    }
    negated = be.op == BinaryOp::StrictNotEqual || be.op == BinaryOp::NotEqual;
    // `x === undefined` / `x === null` narrows the same way as a `typeof`
    // guard: the equal branch sees the nullish member, the other branch the
    // remaining (non-nullish) shape.
    Expr* idSide = be.left.get();
    Expr* nullSide = be.right.get();
    const auto nullish = [](const Expr* e) {
        return e && (e->kind == NodeKind::NullLiteral ||
                     e->kind == NodeKind::UndefinedLiteral);
    };
    if (!nullish(nullSide)) std::swap(idSide, nullSide);
    if (idSide && idSide->kind == NodeKind::Identifier && nullish(nullSide)) {
        name = static_cast<Identifier&>(*idSide).name;
        type = nullSide->kind == NodeKind::NullLiteral ? Type::nullType()
                                                       : Type::undefined();
        return true;
    }
    Expr* typeofSide = be.left.get();
    Expr* strSide = be.right.get();
    if (strSide && strSide->kind != NodeKind::StringLiteral) {
        std::swap(typeofSide, strSide);
    }
    if (!typeofSide || !strSide) return false;
    if (typeofSide->kind != NodeKind::UnaryExpr ||
        strSide->kind != NodeKind::StringLiteral) {
        return false;
    }
    auto& ue = static_cast<UnaryExpr&>(*typeofSide);
    if (ue.op != UnaryOp::Typeof || !ue.operand ||
        ue.operand->kind != NodeKind::Identifier) {
        return false;
    }
    const std::string& lit = static_cast<StringLiteral&>(*strSide).value;
    Type prim;
    if (lit == "number") prim = Type::number();
    else if (lit == "string") prim = Type::string();
    else if (lit == "boolean") prim = Type::boolean();
    else if (lit == "undefined") prim = Type::undefined();
    else if (lit == "object") prim = ambient();
    else if (lit == "function") prim = Type::unknown();
    else return false;
    name = static_cast<Identifier&>(*ue.operand).name;
    type = prim;
    return true;
}

void Analyzer::analyzeWhile(WhileStatement& w) {
    if (w.condition) checkCondition(inferExpr(*w.condition), w.condition->span);
    ++loopDepth_;
    if (w.body) visitStmt(*w.body);
    --loopDepth_;
}

void Analyzer::analyzeDoWhile(DoWhileStatement& d) {
    ++loopDepth_;
    if (d.body) visitStmt(*d.body);
    --loopDepth_;
    if (d.condition) checkCondition(inferExpr(*d.condition), d.condition->span);
}

void Analyzer::analyzeFor(ForStatement& stmt) {
    pushScope();
    if (stmt.init) {
        if (stmt.init->kind == NodeKind::VariableDeclaration) {
            declareVariableDeclaration(
                static_cast<VariableDeclaration&>(*stmt.init));
        } else if (stmt.init->kind == NodeKind::ExpressionStatement) {
            auto& es = static_cast<ExpressionStatement&>(*stmt.init);
            if (es.expression) inferExpr(*es.expression);
        } else {
            // init may be a bare expression node.
            if (auto* e = dynamicExpr(*stmt.init)) inferExpr(*e);
        }
    }
    if (stmt.condition) {
        inferExpr(*stmt.condition);
        checkCondition(typeOfExpr(*stmt.condition), stmt.condition->span);
    }
    if (stmt.update) inferExpr(*stmt.update);
    // The for-loop range: if the header matches the recognized pattern,
    // remember the bound for `typedIndexRead` while the body is checked.
    std::optional<Analyzer::IndexRangeFact> range = forLoopRange(stmt);
    if (range) indexRanges_.push_back(*range);
    ++loopDepth_;
    if (stmt.body) visitStmt(*stmt.body);
    --loopDepth_;
    if (range) indexRanges_.pop_back();
    popScope();
}

void Analyzer::analyzeForOf(ForOfStatement& stmt) {
    pushScope();
    Type rt = stmt.right ? inferExpr(*stmt.right) : Type::unknown();
    Type elem = Type::unknown();
    if (rt.kind == TypeKind::Array) {
        elem = elemOf(rt);
    } else if (rt.kind == TypeKind::String) {
        // `for (const ch of s)`: the string's code points, in order (the JS
        // for-of order; docs 09: cp3 strings are UTF-8, so the element is a
        // whole character).
        elem = Type::string();
    } else if (isTupleType(rt)) {
        if (Shape* sh = shapeOf(rt)) {
            std::vector<Type> es = sh->tupleElems;
            elem = es.empty() ? Type::unknown() : Type::unionOf(std::move(es));
        }
    } else if (rt.isUnknown() || isAmbientType(rt)) {
        elem = ambient();
    }
    // `for await` binds the AWAITED value: a promise resolves to its value
    // (D41), anything else is its own value.
    if (stmt.isAwait && isPromiseType(elem)) elem = promiseValue(elem);
    declareForBinding(stmt.left.get(), elem);
    ++loopDepth_;
    if (stmt.body) visitStmt(*stmt.body);
    --loopDepth_;
    popScope();
}

void Analyzer::analyzeForIn(ForInStatement& stmt) {
    pushScope();
    Type rt = stmt.right ? inferExpr(*stmt.right) : Type::unknown();
    checkBannedForIn(rt, stmt.right ? stmt.right->span : stmt.span);
    // The key type: the indices of an ordered collection (an array, a Set)
    // are numbers, a `Map` iterates its own key type, and anything else has
    // no key set the compiler knows (the codegen reports it).
    Type key = Type::string();
    if (rt.kind == TypeKind::Array) {
        key = Type::number();
    } else if (rt.kind == TypeKind::Class && rt.name == "Map" &&
               !rt.parts.empty()) {
        key = rt.parts.front();
    } else if (rt.kind == TypeKind::Class && rt.name == "Set") {
        key = Type::number();
    }
    declareForBinding(stmt.left.get(), key);
    ++loopDepth_;
    if (stmt.body) visitStmt(*stmt.body);
    --loopDepth_;
    popScope();
}

void Analyzer::analyzeSwitch(SwitchStatement& stmt) {
    if (stmt.discriminant) inferExpr(*stmt.discriminant);
    ++switchDepth_;
    for (auto& c : stmt.cases) {
        if (!c) continue;
        if (c->test) inferExpr(*c->test);
        pushScope();
        checkStmts(c->statements);
        popScope();
    }
    --switchDepth_;
}

void Analyzer::analyzeReturn(ReturnStatement& stmt) {
    Type argType = Type::unknown();
    bool hasArg = false;
    if (stmt.argument) {
        argType = inferExpr(*stmt.argument);
        hasArg = true;
    }
    if (funcStack_.empty()) {
        report(diagcode::kReturnOutsideFunction, stmt.span,
               "return outside of a function");
        return;
    }
    FuncCtx& ctx = funcStack_.back();
    if (ctx.annotated) {
        if (ctx.ret.isVoid()) {
            if (hasArg) {
                report(diagcode::kNotAssignable, stmt.span,
                       "a void function cannot return a value");
            }
        } else if (hasArg) {
            // A tuple return type takes its element types from the literal
            // positionally; the inferred array type cannot express that, so
            // the tuple path checks the literal element by element.
            if (isTupleType(ctx.ret) &&
                stmt.argument->kind == NodeKind::ArrayLiteral) {
                checkArrayLiteral(static_cast<ArrayLiteral&>(*stmt.argument),
                                  ctx.ret);
            } else {
                checkAssign(argType, ctx.ret, stmt.span, false);
            }
        } else if (!ctx.ret.isVoid() && !ctx.ret.isUndefined() &&
                   !ctx.ret.isUnknown()) {
            checkAssign(Type::undefined(), ctx.ret, stmt.span, false);
        }
        ctx.sawReturn = true;
    } else {
        ctx.sawReturn = true;
        if (hasArg) ctx.returnTypes.push_back(widen(argType));
    }
}

void Analyzer::analyzeTry(TryStatement& stmt) {
    if (stmt.tryBlock) visitStmt(*stmt.tryBlock);
    if (stmt.handler) {
        pushScope();
        if (stmt.handler->param) {
            // v0.2: every thrown value is an Error handle, so a catch binds
            // `Error` directly (no narrowing / `instanceof` required).
            declarePattern(stmt.handler->param.get(),
                           Type::named(TypeKind::Class, "Error"), false, 0);
        }
        if (stmt.handler->body) visitStmt(*stmt.handler->body);
        popScope();
    }
    if (stmt.finallyBlock) visitStmt(*stmt.finallyBlock);
}

// =============================================================================
// Functions + return analysis
// =============================================================================
void Analyzer::analyzeFunctionBody(FunctionDeclaration& fn) {
    const FuncSig sig = sigs_.count(fn.nodeId) ? sigs_[fn.nodeId] : FuncSig{};
    // A generic function's body resolves its own type parameters (with their
    // constraints), even though every function in the block was hoisted first.
    if (!fn.typeParams.empty()) registerTypeParams(fn.typeParams);
    pushScope();
    for (std::size_t i = 0; i < fn.params.size() && i < sig.params.size(); ++i) {
        Parameter* p = fn.params[i].get();
        if (!p) continue;
        Type t = sig.params[i];
        if (p->isRest) {
            t = Type::array(sig.params[i]);
            declarePattern(p->name.get(), t, false, p->nodeId);
        } else {
            // A default value makes the parameter optional at the call site,
            // but inside the body it is always bound to the declared
            // (non-nullish) type. Only an explicit `?` adds nullish.
            t = nullableIf(p->optional, t);
            declarePattern(p->name.get(), t, false, p->nodeId);
        }
    }
    FuncCtx ctx;
    ctx.annotated = sig.annotatedReturn;
    // An async function's `return` yields the Promise's value type (spec §14).
    ctx.ret = sig.isAsync ? sig.asyncValueRet : sig.ret;
    // Only an async function may await inside its body.
    ctx.isAsync = sig.isAsync;
    funcStack_.push_back(ctx);
    // The body is a function of its own: a `break`/`continue` inside it binds
    // to its own loops, never to a loop of the enclosing function.
    const FunctionContext outer = enterFunctionBody();
    if (fn.body) visitStmt(*fn.body);
    leaveFunctionBody(outer);
    FuncCtx finalCtx = funcStack_.back();
    funcStack_.pop_back();
    popScope();

    Type inferredRet = finalCtx.ret;
    if (!sig.annotatedReturn) {
        inferredRet = finalCtx.returnTypes.empty()
                          ? Type::voidType()
                          : Type::unionOf(finalCtx.returnTypes);
    }
    updateFunctionReturnType(fn.nodeId,
                             sig.isAsync ? promiseOf(inferredRet) : inferredRet);
    if (sig.annotatedReturn && !inferredRet.isVoid() && !inferredRet.isUndefined() &&
        inferredRet.kind != TypeKind::Unknown) {
        if (fn.body && !allPathsReturn(fn.body.get())) {
            report(diagcode::kMissingReturn, fn.span,
                   "function '" + fn.name +
                       "' is declared to return a value but may fall off "
                       "the end of its body");
        }
    }
}

}  // namespace cp3
