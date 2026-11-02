// =============================================================================
// src/codegen/src/emit/stmt.cpp - codegen statement lowering (STYLE.md §8).
//
// Statements, blocks and control flow: variable declarations (including the
// M1 type spelling / nullish folding), if/while/do/for, return (with the
// void-vs-valued signature check), break/continue, and nested function
// declarations (lowered to local closures). Every statement kind the parser
// produces has a lowering; an unknown kind is an internal error.
// =============================================================================
#include "detail/generator/generator.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "cp3/diag/diagnostic_codes.hpp"
#include "detail/generator/cpp_writer.h"
#include "detail/generator/mangle.h"
#include "detail/expr/pattern_emitter.h"
#include "detail/generator/type_map.h"
#include "detail/worker/worker.h"

namespace cp3 {

using namespace detail;

namespace {

// A function declared inside a block lowers to a local closure: C++ has no
// nested function definitions. A fully typed signature uses a `std::function`
// variable so the closure can be hoisted (and call itself); a generic or
// deduced-return signature keeps a plain generic lambda.
struct LocalFunction {
    const FunctionDeclaration* fd = nullptr;
    bool viaStdFunction = false;
    std::string ret;
    std::string name;
    std::vector<std::string> paramTypes;
    Type retType;
};

// A declared tuple's C++ spelling. `classify` keeps the all-primitive gate for
// ordinary tuple values, but a declaration whose elements are records or class
// handles is still a `std::tuple` of those handles.
std::optional<CppInfo> tupleDeclaredInfo(const Type& t) {
    std::string cpp;
    if (!isTupleType(t) || !tupleCpp(t, cpp)) return std::nullopt;
    CppInfo info;
    info.shape = Shape::Object;
    info.representable = true;
    info.cpp = cpp;
    info.type = t;
    return info;
}

}  // namespace

void Generator::stmt(const Stmt& s, std::string& out, int indent) {
    // One source-mapping directive per statement (--debug only).
    emitLineDirective(s, out);
    // Loops, switch, labels and break/continue carry generated labels and flow
    // frames; their lowering unit runs before the ordinary arm table.
    if (emitControlDispatch(s, out, indent)) return;

    // A function declaration inside a block is a local closure. Prepare it
    // once so its return-type diagnostic is reported once, then emit the
    // declaration (a std::function slot when the signature is fully typed) and
    // the definition (the lambda).
    const auto prepareLocalFunction =
        [this](const FunctionDeclaration& fd) -> LocalFunction {
        if (intOn()) intFail();
        LocalFunction fn;
        fn.fd = &fd;
        fn.name = mapIdent(fd.name);
        fn.ret = functionReturnType(fd);
        fn.retType = returnValueType(fd).value_or(Type::unknown());
        bool generic = false;
        for (std::size_t i = 0; i < fd.params.size(); ++i) {
            const Parameter* p = fd.params[i].get();
            if (p == nullptr) continue;
            std::string type = parameterCppType(*p, fd, i);
            if (type.empty()) {
                generic = true;
                break;
            }
            if (p->isRef) type += "&";
            fn.paramTypes.push_back(std::move(type));
        }
        fn.viaStdFunction = !generic && fn.ret != "auto";
        return fn;
    };
    const auto declareLocalFunction = [this](const LocalFunction& fn,
                                             std::string& out, int indent) {
        std::vector<Type> params;
        params.reserve(fn.fd->params.size());
        for (std::size_t i = 0; i < fn.fd->params.size(); ++i) {
            const Parameter* p = fn.fd->params[i].get();
            params.push_back(p ? parameterType(*p, *fn.fd, i)
                                     .value_or(Type::unknown())
                               : Type::unknown());
        }
        declare(fn.fd->name, Type::function(std::move(params), fn.retType));
        if (!fn.viaStdFunction) return;
        line(out, indent,
             "std::function<" + fn.ret + "(" +
                 joinStrings(fn.paramTypes, ", ") + ")> " + fn.name + ";");
    };
    const auto defineLocalFunction = [this](const LocalFunction& fn,
                                            std::string& out, int indent) {
        std::vector<std::string> params;
        params.reserve(fn.fd->params.size());
        for (std::size_t i = 0; i < fn.fd->params.size(); ++i) {
            const Parameter* p = fn.fd->params[i].get();
            if (p == nullptr) continue;
            if (parameterCppType(*p, *fn.fd, i).empty()) {
                // A generic lambda parameter: `auto` deduces the same type
                // the function template would have named.
                const Identifier* id = parameterIdentifier(*p);
                const std::string name =
                    id ? mapIdent(id->name)
                       : "cp3_param_" + std::to_string(i);
                params.push_back("auto " + name);
            } else {
                params.push_back(parameterText(*p, *fn.fd, i, true));
            }
        }
        std::string head =
            fn.viaStdFunction
                ? fn.name + " = [&](" + joinStrings(params, ", ") + ")"
                : "auto " + fn.name + " = [&](" +
                      joinStrings(params, ", ") + ")";
        if (fn.ret != "auto") head += " -> " + fn.ret;
        emitLineDirective(*fn.fd, out);
        line(out, indent, head + " {");
        const std::string body = plainBody(*fn.fd, fn.ret);
        out += indentText(body, std::string(
                                    static_cast<std::size_t>(indent) * 4, ' '));
        line(out, indent, "};");
    };

    switch (s.kind) {
        case NodeKind::BlockStatement: {
            pushScope();
            line(out, indent, "{");
            const auto& block = static_cast<const BlockStatement&>(s);
            // Function declarations are hoisted within their block (sema
            // hoists them before checking the body). A typed closure gets its
            // std::function slot first, so sibling closures and earlier
            // statements can call it; the lambda itself is assigned at the
            // declaration point, where the locals it captures are in scope.
            std::vector<LocalFunction> localFunctions;
            for (const auto& child : block.body) {
                if (child && child->kind == NodeKind::FunctionDeclaration) {
                    localFunctions.push_back(prepareLocalFunction(
                        static_cast<const FunctionDeclaration&>(*child)));
                }
            }
            for (const LocalFunction& fn : localFunctions) {
                declareLocalFunction(fn, out, indent + 1);
            }
            for (std::size_t i = 0; i < block.body.size(); ++i) {
                const Stmt* child = block.body[i].get();
                if (!child) continue;
                if (child->kind == NodeKind::FunctionDeclaration) {
                    for (const LocalFunction& fn : localFunctions) {
                        if (fn.fd == child) {
                            defineLocalFunction(fn, out, indent + 1);
                            break;
                        }
                    }
                    continue;
                }
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
        case NodeKind::FunctionDeclaration: {
            // A nested function outside a block (an `if` branch without
            // braces): no hoisting scope exists, so emit the closure here.
            const auto& fd = static_cast<const FunctionDeclaration&>(s);
            const LocalFunction fn = prepareLocalFunction(fd);
            declareLocalFunction(fn, out, indent);
            defineLocalFunction(fn, out, indent);
            return;
        }
        default:
            // Every statement kind the parser produces has a lowering above;
            // reaching this arm is an internal invariant violation, not an
            // unimplemented language feature.
            reportCodegenError(s, diagcode::kInternalError,
                               "internal error: unsupported statement kind");
            line(out, indent, "/* unsupported statement */");
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
        emitDeclaratorStmt(*decl, vd.kind, out, indent);
    }
}

// One declarator as a statement. A plain binding is one C++ declaration; a
// binding pattern (`let [a, b] = ...` / `let { a } = ...`) unpacks in its own
// unit (the source temporary plus one binding per name).
void Generator::emitDeclaratorStmt(const VariableDeclarator& d, VarKind kind,
                                   std::string& out, int indent) {
    if (stackAliasDecls_.count(d.nodeId) != 0 && d.init &&
        d.init->kind == NodeKind::Identifier && d.id &&
        d.id->kind == NodeKind::Identifier) {
        const std::string name =
            mapIdent(static_cast<const Identifier&>(*d.id).name);
        line(out, indent, "auto* const " + name + " = " + expr(*d.init) + ";");
        declareBinding(d);
        return;
    }
    if (stackAllocDecls_.count(d.nodeId) != 0 && d.init && d.id &&
        d.id->kind == NodeKind::Identifier) {
        const std::string name =
            mapIdent(static_cast<const Identifier&>(*d.id).name);
        const std::string local = freshLabel("stack_object_");
        if (d.init->kind == NodeKind::ArrayLiteral) {
            const Type* bindingType = sema_.typeOf(d.nodeId);
            Type array = bindingType ? *bindingType : effectiveType(*d.init);
            if (array.kind == TypeKind::Array && !array.parts.empty()) {
                const CppInfo element = classify(array.parts.front());
                const auto& literal = static_cast<const ArrayLiteral&>(*d.init);
                if (element.representable && literal.elements.empty()) {
                    std::string elementCpp = element.cpp;
                    if (stackValueArrayDecls_.count(d.nodeId) != 0) {
                        constexpr const char* refPrefixes[] = {
                            "::cp3::runtime::Ref<",
                            "::cp3::runtime::LocalRef<"};
                        std::size_t prefixSize = 0;
                        for (const char* prefix : refPrefixes) {
                            const std::size_t size =
                                std::char_traits<char>::length(prefix);
                            if (elementCpp.rfind(prefix, 0) == 0) {
                                prefixSize = size;
                                break;
                            }
                        }
                        if (prefixSize != 0 &&
                            elementCpp.size() > prefixSize &&
                            elementCpp.back() == '>') {
                            elementCpp = elementCpp.substr(
                                prefixSize, elementCpp.size() - prefixSize - 1);
                        }
                    }
                    line(out, indent, "std::vector<" + elementCpp + "> " +
                                          local + "{};");
                    line(out, indent, "auto* const " + name + " = &" + local + ";");
                    declareBinding(d);
                    return;
                }
            }
        } else if (d.init->kind == NodeKind::NewExpr) {
            const auto& n = static_cast<const NewExpr&>(*d.init);
            const Type objectType = effectiveType(n);
            const CppInfo objectInfo = classify(objectType);
            const ClassModel* model =
                n.callee && n.callee->kind == NodeKind::Identifier
                    ? detail::ModelRegistry::get().classByName(
                          static_cast<const Identifier&>(*n.callee).name)
                    : nullptr;
            if (model && objectInfo.representable &&
                objectInfo.shape == Shape::ClassRef) {
                std::string init = emitModelNew(n);
                std::string concreteClass = objectInfo.cpp;
                constexpr const char* refPrefixes[] = {
                    "::cp3::runtime::Ref<",
                    "::cp3::runtime::LocalRef<"};
                std::size_t refPrefixSize = 0;
                for (const char* prefix : refPrefixes) {
                    const std::size_t size =
                        std::char_traits<char>::length(prefix);
                    if (concreteClass.rfind(prefix, 0) == 0) {
                        refPrefixSize = size;
                        break;
                    }
                }
                if (refPrefixSize != 0 &&
                    concreteClass.size() > refPrefixSize &&
                    concreteClass.back() == '>') {
                    concreteClass = concreteClass.substr(
                        refPrefixSize, concreteClass.size() - refPrefixSize - 1);
                }
                const std::string factory =
                    std::string("::cp3::runtime::") +
                    (model->localRef ? "makeLocalRef<" : "makeRef<") +
                    concreteClass + ">(";
                if (init.rfind(factory, 0) == 0 &&
                    init.size() > factory.size() && init.back() == ')') {
                    init = concreteClass + "(" +
                           init.substr(factory.size(),
                                       init.size() - factory.size() - 1) +
                           ")";
                    // `const box` prevents rebinding the handle, not mutation
                    // through the object reference. Keep the stack object
                    // mutable because emitted member methods are not const.
                    line(out, indent, concreteClass + " " + local + " = " +
                                          init + ";");
                    line(out, indent,
                         "auto* const " + name + " = &" + local + ";");
                    declareBinding(d);
                    return;
                }
                // A special constructor lowering keeps the owning fallback.
            }
        }
    }
    if (emitDestructureDecl(d, kind, out, indent)) return;
    const std::string text = declaratorText(d, kind);
    if (!text.empty()) line(out, indent, text + ";");
    declareBinding(d);
}

// The `for` header's init text. A binding pattern (`for (let [a, b] = ...; ...)`)
// cannot live in a C++ `for` header: such an init is emitted as statements
// before the loop, in the loop's own scope, and the header stays empty.
std::string Generator::forInitText(const VariableDeclaration& vd,
                                   std::string& out, int indent) {
    bool pattern = false;
    std::vector<std::string> parts;
    for (const auto& d : vd.declarations) {
        if (!d) continue;
        if (d->id && d->id->kind != NodeKind::Identifier) {
            pattern = true;
            break;
        }
        parts.push_back(declaratorText(*d, vd.kind));
    }
    if (pattern) {
        for (const auto& d : vd.declarations) {
            if (d) emitDeclaratorStmt(*d, vd.kind, out, indent);
        }
        return std::string();
    }
    for (const auto& d : vd.declarations) {
        if (d) declareBinding(*d);
    }
    return joinStrings(parts, ", ");
}

std::string Generator::declaratorText(const VariableDeclarator& d, VarKind kind) {
    if (!d.id || d.id->kind != NodeKind::Identifier) {
        // A binding pattern is lowered by emitDeclaratorStmt (the destructuring
        // path); this helper only spells a plain-name declarator.
        return std::string();
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
    // A generic arrow binds a C++ generic lambda, whose `operator()` is not
    // const-qualified: the binding cannot be a `const` C++ object. cp3's own
    // const-ness is enforced by sema (a reassignment is CP3408).
    const std::string cv =
        (kind == VarKind::Const && !isGenericArrowInit(d)) ? "const " : "";
    // A declared tuple has a concrete `std::tuple` spelling even when an
    // element is a record/class handle. Any other declared type without a C++
    // spelling is a hard error: silently deducing a narrower initializer type
    // would not enforce the annotation.
    std::optional<CppInfo> tupleCi;
    if (d.type) {
        if (auto dt = typeOfNode(*d.type)) {
            tupleCi = tupleDeclaredInfo(*dt);
            if (!tupleCi && !classify(*dt).representable && !dt->isUnknown() &&
                !dt->isError() && !isChannelHandleType(*dt)) {
                reportUnsupported(d, diagcode::kTypeMismatch,
                                  "type mismatch: this declared type has no "
                                  "C++ spelling");
            }
        }
    }
    const CppInfo ci = tupleCi ? *tupleCi : bindingCpp(d);
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
        // Always lower the returned expression so its own unsupported parts
        // report their own diagnostics instead of being silently dropped.
        const std::string value =
            currentReturnTuple_ && r.argument->kind == NodeKind::ArrayLiteral
                ? emitTupleLiteral(static_cast<const ArrayLiteral&>(*r.argument))
                : expr(*r.argument);
        if (currentReturn_ == "void") {
            // The enclosing signature is `void` either because the cp3
            // return type has no C++ spelling (already reported where the
            // signature was emitted) or because the function is genuinely
            // void. Only the latter is a type error here; keep the emitted
            // C++ valid in both cases.
            if (!currentReturnType_ || currentReturnType_->isVoid() ||
                currentReturnType_->isUnknown()) {
                reportTypeError(
                    r, "type mismatch: a void function cannot return a value");
            }
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
         "throw ::cp3::runtime::Thrown::makeError(" + expr(*t.argument) +
             ");");
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
        // Every cp3-thrown value is an Error handle; native exceptions are
        // normalized to one. `catch (e)` therefore binds `Ref<Error>` directly.
        bool bound = cc.param && cc.param->kind == NodeKind::Identifier;
        std::string boundName;
        if (bound) {
            boundName = static_cast<const Identifier&>(*cc.param).name;
            line(out, indent + 1,
                 "auto " + mapIdent(boundName) +
                     " = ::cp3::runtime::Thrown::fromCurrentException()"
                     ".asError();");
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
