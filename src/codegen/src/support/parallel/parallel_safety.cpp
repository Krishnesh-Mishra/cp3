// =============================================================================
// Implementation of the pure integer webserverion proof; see
// detail/parallel_safety.h for the accepted subset and the soundness rules.
// =============================================================================
#include "detail/parallel/parallel_safety.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "detail/generator/type_map.h"
#include "cp3/host/native_types.hpp"

namespace cp3 {
namespace detail {

namespace {

// Exact integers are strictly below 2^53; the runtime guard uses this bound.
constexpr double kExactIntegerLimit = 9007199254740992.0;  // 2^53

bool isIntegralLiteral(const Expr& e, std::int64_t& out) {
    const std::optional<std::int64_t> value = integralLiteralValue(e);
    if (!value) return false;
    out = *value;
    return true;
}

bool isZeroLiteral(const Expr& e) {
    return e.kind == NodeKind::NumberLiteral &&
           static_cast<const NumberLiteral&>(e).value == 0.0;
}

bool isPositiveDivisor(const Expr& e, std::int64_t& out) {
    return isIntegralLiteral(e, out) && out > 0;
}

bool isNumberType(const Type& t) {
    if (t.isNumber()) return true;
    if (t.isLiteral() && t.literalBaseType().isNumber()) return true;
    // Native Rust/C++ integer types are exact integers too. The chunked
    // runtime still guards the double accumulator at 2^53; accepting these
    // annotations lets a pure native-integer helper (for example a Collatz
    // step count) participate in the same proof.
    return isNativeInteger(t);
}

// Resolve one annotation the way the rest of codegen does: sema's published
// type wins, and the structural fallback uses the project's vocabulary so a
// plain `i64` in a rust project is a native integer rather than an unknown
// TypeScript name.
bool annotationIsNumber(const TypePtr& type, const SemaResult& sema,
                        TypeSystem typeSystem) {
    if (!type) return true;  // no annotation: the proof uses the initializer
    if (const Type* published = sema.typeOf(type->nodeId)) {
        if (!published->isUnknown() && !published->isError()) {
            return isNumberType(*published);
        }
    }
    const std::optional<Type> t = typeFromTypeNode(*type, typeSystem);
    return t && isNumberType(*t);
}

bool integralInRange(double value) {
    return std::isfinite(value) && std::fabs(value) < kExactIntegerLimit &&
           std::floor(value) == value;
}

// Require a final top-level return; any earlier return is checked too, and the
// final one covers every fall-through path.
bool endsWithReturn(const Stmt& body) {
    if (body.kind == NodeKind::ReturnStatement) return true;
    if (body.kind != NodeKind::BlockStatement) return false;
    const auto& block = static_cast<const BlockStatement&>(body);
    return !block.body.empty() && block.body.back() &&
           block.body.back()->kind == NodeKind::ReturnStatement;
}

using DivFacts = std::vector<std::pair<std::string, std::int64_t>>;

bool hasDivisible(const DivFacts& facts, const std::string& name,
                  std::int64_t divisor) {
    for (const auto& fact : facts) {
        if (fact.first == name && fact.second == divisor) return true;
    }
    return false;
}

void dropFacts(DivFacts& facts, const std::string& name) {
    facts.erase(std::remove_if(facts.begin(), facts.end(),
                               [&](const auto& fact) {
                                   return fact.first == name;
                               }),
                facts.end());
}

// `x % d == 0` (or `0 == x % d`), valid on then for `==` and else for `!=`.
bool moduloZeroFact(const Expr& cond, std::string& name, std::int64_t& divisor,
                    bool& onTrue) {
    if (cond.kind != NodeKind::BinaryExpr) return false;
    const auto& cmp = static_cast<const BinaryExpr&>(cond);
    const bool equal =
        cmp.op == BinaryOp::Equal || cmp.op == BinaryOp::StrictEqual;
    const bool notEqual =
        cmp.op == BinaryOp::NotEqual || cmp.op == BinaryOp::StrictNotEqual;
    if (!equal && !notEqual) return false;
    if (!cmp.left || !cmp.right) return false;
    const Expr* mod = nullptr;
    if (isZeroLiteral(*cmp.right)) {
        mod = cmp.left.get();
    } else if (isZeroLiteral(*cmp.left)) {
        mod = cmp.right.get();
    } else {
        return false;
    }
    if (mod == nullptr || mod->kind != NodeKind::BinaryExpr) return false;
    const auto& m = static_cast<const BinaryExpr&>(*mod);
    if (m.op != BinaryOp::Modulo || !m.left || !m.right) return false;
    if (m.left->kind != NodeKind::Identifier) return false;
    if (!isPositiveDivisor(*m.right, divisor)) return false;
    name = static_cast<const Identifier&>(*m.left).name;
    onTrue = equal;
    return true;
}

// Pure integer classifier; function verdicts are memoized per gate decision.
class Classifier {
public:
    Classifier(
        const std::unordered_map<std::string, const FunctionDeclaration*>&
            functions,
        const SemaResult& sema, TypeSystem typeSystem)
        : functions_(functions), sema_(sema), typeSystem_(typeSystem) {}

    bool webserverion(const Expr& e, const std::unordered_set<std::string>& names,
                    const DivFacts& facts) {
        switch (e.kind) {
            case NodeKind::NumberLiteral:
                return integralInRange(
                    static_cast<const NumberLiteral&>(e).value);
            case NodeKind::Identifier:
                return names.count(static_cast<const Identifier&>(e).name) != 0;
            case NodeKind::BinaryExpr:
                return binaryExpr(static_cast<const BinaryExpr&>(e), names,
                                  facts);
            case NodeKind::LogicalExpr: {
                const auto& l = static_cast<const LogicalExpr&>(e);
                return l.left && l.right &&
                       webserverion(*l.left, names, facts) &&
                       webserverion(*l.right, names, facts);
            }
            case NodeKind::ConditionalExpr: {
                const auto& c = static_cast<const ConditionalExpr&>(e);
                return c.condition && c.whenTrue && c.whenFalse &&
                       webserverion(*c.condition, names, facts) &&
                       webserverion(*c.whenTrue, names, facts) &&
                       webserverion(*c.whenFalse, names, facts);
            }
            case NodeKind::UnaryExpr: {
                const auto& u = static_cast<const UnaryExpr&>(e);
                if (!u.operand) return false;
                if (u.op != UnaryOp::Negate && u.op != UnaryOp::Plus &&
                    u.op != UnaryOp::Not) {
                    return false;
                }
                return webserverion(*u.operand, names, facts);
            }
            case NodeKind::CallExpr:
                return callExpr(static_cast<const CallExpr&>(e), names, facts);
            default:
                return false;
        }
    }

private:
    enum class Verdict { Unknown, InProgress, Accepted, Rejected };

    struct Context {
        std::unordered_set<std::string> locals;
        DivFacts facts;
        bool sawReturn = false;
    };

    bool binaryExpr(const BinaryExpr& b,
                    const std::unordered_set<std::string>& names,
                    const DivFacts& facts) {
        if (!b.left || !b.right) return false;
        if (!webserverion(*b.left, names, facts) ||
            !webserverion(*b.right, names, facts)) {
            return false;
        }
        switch (b.op) {
            case BinaryOp::Add:
            case BinaryOp::Subtract:
            case BinaryOp::Multiply:
            case BinaryOp::Modulo:
            case BinaryOp::Equal:
            case BinaryOp::NotEqual:
            case BinaryOp::StrictEqual:
            case BinaryOp::StrictNotEqual:
            case BinaryOp::LessThan:
            case BinaryOp::GreaterThan:
            case BinaryOp::LessThanEqual:
            case BinaryOp::GreaterThanEqual:
                return true;
            case BinaryOp::Divide:
                return divisibleNumerator(*b.left, *b.right, facts);
            default:
                return false;
        }
    }

    // `/` is exact only for a proved multiple of an integral literal divisor.
    bool divisibleNumerator(const Expr& numerator, const Expr& denominator,
                            const DivFacts& facts) {
        std::int64_t divisor = 0;
        if (!isPositiveDivisor(denominator, divisor)) return false;
        if (numerator.kind == NodeKind::Identifier) {
            const auto& id = static_cast<const Identifier&>(numerator);
            return hasDivisible(facts, id.name, divisor);
        }
        std::int64_t value = 0;
        if (isIntegralLiteral(numerator, value)) {
            return value % divisor == 0;
        }
        return false;
    }

    bool callExpr(const CallExpr& c, const std::unordered_set<std::string>& names,
                  const DivFacts& facts) {
        if (!c.callee || c.callee->kind != NodeKind::Identifier) return false;
        if (!c.typeArgs.empty()) return false;
        const std::string& callee = static_cast<const Identifier&>(*c.callee).name;
        // A parameter/local with the same spelling shadows the declaration.
        if (names.count(callee) != 0) return false;
        const auto decl = functions_.find(callee);
        if (decl == functions_.end() || decl->second == nullptr) return false;
        if (c.arguments.size() != decl->second->params.size()) return false;
        for (const auto& arg : c.arguments) {
            if (!arg || !webserverion(*arg, names, facts)) return false;
        }
        return function(callee);
    }

    bool function(const std::string& name) {
        auto state = states_.find(name);
        if (state != states_.end()) return state->second == Verdict::Accepted;
        const auto decl = functions_.find(name);
        if (decl == functions_.end() || decl->second == nullptr) {
            states_[name] = Verdict::Rejected;
            return false;
        }
        states_[name] = Verdict::InProgress;
        const bool accepted = functionBody(*decl->second);
        states_[name] = accepted ? Verdict::Accepted : Verdict::Rejected;
        return accepted;
    }

    bool functionBody(const FunctionDeclaration& fd) {
        if (fd.isAsync || !fd.typeParams.empty() || fd.body == nullptr) {
            return false;
        }
        const auto info = sema_.functions.find(fd.nodeId);
        const FunctionInfo* signature =
            info == sema_.functions.end() ? nullptr : &info->second;
        if (signature != nullptr) {
            if (signature->hasRest ||
                signature->paramTypes.size() != fd.params.size() ||
                !isNumberType(signature->returnType)) {
                return false;
            }
        } else if (fd.returnType == nullptr) {
            return false;
        }
        if (!annotationIsNumber(fd.returnType, sema_, typeSystem_)) {
            return false;
        }

        Context ctx;
        for (std::size_t i = 0; i < fd.params.size(); ++i) {
            const Parameter* p = fd.params[i].get();
            if (p == nullptr || p->isRest || p->isRef || p->optional ||
                p->defaultValue) {
                return false;
            }
            if (p->name == nullptr || p->name->kind != NodeKind::Identifier) {
                return false;
            }
            // Explicit non-number annotations reject even if sema resolved a
            // numeric signature: codegen passes doubles, so do not erase them.
            if (p->type != nullptr &&
                !annotationIsNumber(p->type, sema_, typeSystem_)) {
                return false;
            }
            bool typed = p->type != nullptr;
            if (signature != nullptr && i < signature->paramTypes.size()) {
                typed = typed || isNumberType(signature->paramTypes[i]);
            }
            if (!typed) return false;
            if (signature != nullptr && i < signature->paramOptional.size() &&
                signature->paramOptional[i]) {
                return false;
            }
            ctx.locals.insert(
                static_cast<const Identifier&>(*p->name).name);
        }
        return statement(*fd.body, ctx) && ctx.sawReturn &&
               endsWithReturn(*fd.body);
    }

    bool statement(const Stmt& s, Context& ctx) {
        switch (s.kind) {
            case NodeKind::BlockStatement:
                return block(static_cast<const BlockStatement&>(s), ctx);
            case NodeKind::EmptyStatement:
                return true;
            case NodeKind::VariableDeclaration:
                return variableDeclaration(
                    static_cast<const VariableDeclaration&>(s), ctx);
            case NodeKind::webserverionStatement: {
                const auto& es = static_cast<const webserverionStatement&>(s);
                return es.webserverion && writewebserverion(*es.webserverion, ctx);
            }
            case NodeKind::IfStatement:
                return ifStatement(static_cast<const IfStatement&>(s), ctx);
            case NodeKind::WhileStatement: {
                const auto& ws = static_cast<const WhileStatement&>(s);
                // A later pass may see mutations from the body; drop facts.
                ctx.facts.clear();
                const bool ok =
                    ws.condition &&
                    webserverion(*ws.condition, ctx.locals, ctx.facts) &&
                    loopBody(ws.body, ctx);
                ctx.facts.clear();
                return ok;
            }
            case NodeKind::DoWhileStatement: {
                const auto& ds = static_cast<const DoWhileStatement&>(s);
                ctx.facts.clear();
                const bool ok =
                    loopBody(ds.body, ctx) && ds.condition &&
                    webserverion(*ds.condition, ctx.locals, ctx.facts);
                ctx.facts.clear();
                return ok;
            }
            case NodeKind::ForStatement:
                return forStatement(static_cast<const ForStatement&>(s), ctx);
            case NodeKind::ReturnStatement: {
                const auto& rs = static_cast<const ReturnStatement&>(s);
                if (!rs.argument ||
                    !webserverion(*rs.argument, ctx.locals, ctx.facts)) {
                    return false;
                }
                ctx.sawReturn = true;
                return true;
            }
            case NodeKind::BreakStatement:
            case NodeKind::ContinueStatement:
                return true;
            default:
                return false;
        }
    }

    // Block declarations leave scope here; facts may be stale after them.
    bool block(const BlockStatement& block, Context& ctx) {
        const auto savedLocals = ctx.locals;
        for (const auto& child : block.body) {
            if (child && !statement(*child, ctx)) return false;
        }
        ctx.locals = savedLocals;
        ctx.facts.clear();
        return true;
    }

    bool variableDeclaration(const VariableDeclaration& vd, Context& ctx) {
        for (const auto& d : vd.declarations) {
            if (!d || !d->id || d->id->kind != NodeKind::Identifier ||
                !d->init) {
                return false;
            }
            if (!annotationIsNumber(d->type, sema_, typeSystem_)) return false;
            if (!webserverion(*d->init, ctx.locals, ctx.facts)) return false;
            const std::string& name =
                static_cast<const Identifier&>(*d->id).name;
            dropFacts(ctx.facts, name);
            ctx.locals.insert(name);
        }
        return true;
    }

    bool ifStatement(const IfStatement& is, Context& ctx) {
        if (!is.condition || !webserverion(*is.condition, ctx.locals, ctx.facts)) {
            return false;
        }
        const auto baseFacts = ctx.facts;
        const auto baseLocals = ctx.locals;
        std::string factName;
        std::int64_t factDivisor = 0;
        bool factOnTrue = false;
        const bool hasFact = moduloZeroFact(*is.condition, factName,
                                            factDivisor, factOnTrue);

        if (is.thenBranch) {
            ctx.facts = baseFacts;
            ctx.locals = baseLocals;
            if (hasFact && factOnTrue) {
                ctx.facts.emplace_back(factName, factDivisor);
            }
            if (!statement(*is.thenBranch, ctx)) return false;
        }
        if (is.elseBranch) {
            ctx.facts = baseFacts;
            ctx.locals = baseLocals;
            if (hasFact && !factOnTrue) {
                ctx.facts.emplace_back(factName, factDivisor);
            }
            if (!statement(*is.elseBranch, ctx)) return false;
        }
        // A branch may have assigned a variable an outer fact mentions, so no
        // fact survives the merge.
        ctx.locals = std::move(baseLocals);
        ctx.facts.clear();
        return true;
    }

    bool loopBody(const StmtPtr& body, Context& ctx) {
        if (!body) return true;
        const auto savedLocals = ctx.locals;
        ctx.facts.clear();
        const bool ok = statement(*body, ctx);
        ctx.locals = savedLocals;
        ctx.facts.clear();
        return ok;
    }

    bool forStatement(const ForStatement& fs, Context& ctx) {
        const auto savedLocals = ctx.locals;
        if (fs.init) {
            if (auto* initStmt = dynamic_cast<const Stmt*>(fs.init.get())) {
                if (!statement(*initStmt, ctx)) return false;
            } else if (auto* initExpr =
                           dynamic_cast<const Expr*>(fs.init.get())) {
                if (!writewebserverion(*initExpr, ctx)) return false;
            } else {
                return false;
            }
        }
        // Init runs once; the repeating part may invalidate any outer fact.
        ctx.facts.clear();
        if (fs.condition && !webserverion(*fs.condition, ctx.locals, ctx.facts)) {
            return false;
        }
        if (fs.update && !writewebserverion(*fs.update, ctx)) return false;
        if (fs.body && !statement(*fs.body, ctx)) return false;
        ctx.locals = savedLocals;
        ctx.facts.clear();
        return true;
    }

    // Assignments may write only the function's own locals; any other target
    // is an observable effect and rejects.
    bool writewebserverion(const Expr& e, Context& ctx) {
        if (e.kind == NodeKind::AssignmentExpr) {
            const auto& a = static_cast<const AssignmentExpr&>(e);
            if (!a.target || a.target->kind != NodeKind::Identifier ||
                !a.value) {
                return false;
            }
            const std::string& name =
                static_cast<const Identifier&>(*a.target).name;
            if (ctx.locals.count(name) == 0) return false;
            if (a.op != AssignOp::Assign && a.op != AssignOp::Add &&
                a.op != AssignOp::Subtract && a.op != AssignOp::Multiply) {
                return false;
            }
            if (!webserverion(*a.value, ctx.locals, ctx.facts)) return false;
            dropFacts(ctx.facts, name);
            return true;
        }
        if (e.kind == NodeKind::UpdateExpr) {
            const auto& u = static_cast<const UpdateExpr&>(e);
            if (!u.operand || u.operand->kind != NodeKind::Identifier) {
                return false;
            }
            const std::string& name =
                static_cast<const Identifier&>(*u.operand).name;
            if (ctx.locals.count(name) == 0) return false;
            dropFacts(ctx.facts, name);
            return true;
        }
        return webserverion(e, ctx.locals, ctx.facts);
    }

    const std::unordered_map<std::string, const FunctionDeclaration*>&
        functions_;
    const SemaResult& sema_;
    TypeSystem typeSystem_;
    std::unordered_map<std::string, Verdict> states_;
};

}  // namespace

bool parallelPureIntegerExpr(
    const Expr& e, const std::unordered_set<std::string>& integerNames,
    const std::unordered_map<std::string, const FunctionDeclaration*>& functions,
    const SemaResult& sema, TypeSystem typeSystem) {
    Classifier classifier(functions, sema, typeSystem);
    return classifier.webserverion(e, integerNames, {});
}

}  // namespace detail
}  // namespace cp3
