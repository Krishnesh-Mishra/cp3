// =============================================================================
// src/codegen/src/emit/decl.cpp - codegen declaration/definition lowering
// (STYLE.md §8).
//
// Top-level dispatch, exported declarations, and function emission: forward
// declarations + definitions (default arguments live on the forward declaration
// only), parameter type spelling, and return-type resolution (annotated, sema,
// then the first `return` in the body). `async function` bodies lower to C++20
// coroutines whose promise type is the runtime's `Promise<T>` (spec §14).
// Out-of-scope declarations report CP3002.
// =============================================================================
#include "detail/generator.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "detail/cpp_writer.h"
#include "detail/mangle.h"
#include "detail/type_map.h"

namespace cp3 {

using namespace detail;

namespace {

// A module-scope `const` may initialize in place only when its initializer is
// a compile-time value. Anything that reads runtime state (a call,
// interpolation, `process.argv`/`env`) must run inside `main` at the
// statement's source position: a namespace-scope static initializer runs
// before `main`, which is before initArgv and every other runtime store
// exists. This classifies the literal subset (operators over literals
// included) that is safe to hoist.
bool isStaticInitExpr(const Expr& e) {
    switch (e.kind) {
        case NodeKind::NumberLiteral:
        case NodeKind::StringLiteral:
        case NodeKind::BoolLiteral:
        case NodeKind::NullLiteral:
        case NodeKind::UndefinedLiteral:
            return true;
        case NodeKind::UnaryExpr: {
            const auto& u = static_cast<const UnaryExpr&>(e);
            return u.operand && isStaticInitExpr(*u.operand);
        }
        case NodeKind::BinaryExpr: {
            const auto& b = static_cast<const BinaryExpr&>(e);
            return b.left && b.right && isStaticInitExpr(*b.left) &&
                   isStaticInitExpr(*b.right);
        }
        case NodeKind::LogicalExpr: {
            const auto& l = static_cast<const LogicalExpr&>(e);
            return l.left && l.right && isStaticInitExpr(*l.left) &&
                   isStaticInitExpr(*l.right);
        }
        case NodeKind::ConditionalExpr: {
            const auto& c = static_cast<const ConditionalExpr&>(e);
            return c.condition && c.whenTrue && c.whenFalse &&
                   isStaticInitExpr(*c.condition) &&
                   isStaticInitExpr(*c.whenTrue) &&
                   isStaticInitExpr(*c.whenFalse);
        }
        default:
            return false;
    }
}

}  // namespace

// Records every function declared in this unit (including `export function`)
// so a call can be checked against them.
void Generator::collectFunctionNames(const Stmt& s) {
    if (s.kind == NodeKind::FunctionDeclaration) {
        const auto& fd = static_cast<const FunctionDeclaration&>(s);
        functions_.insert(fd.name);
        functionDecls_[fd.name] = &fd;
        if (fd.isAsync) {
            asyncFunctions_.insert(fd.name);
        }
    } else if (s.kind == NodeKind::ExportDeclaration) {
        const auto& ed = static_cast<const ExportDeclaration&>(s);
        if (ed.declaration) collectFunctionNames(*ed.declaration);
    }
}

void Generator::emitTopLevel(const Stmt& s, std::string& decls, std::string& defs,
                             std::string& mainBody) {
    switch (s.kind) {
        case NodeKind::ImportDeclaration:
            return;
        case NodeKind::FunctionDeclaration:
            addFunction(static_cast<const FunctionDeclaration&>(s), decls, defs);
            return;
        case NodeKind::VariableDeclaration:
            // Module-scope bindings are namespace-scope globals: functions
            // declared around them must be able to read them (S1).
            emitModuleVar(static_cast<const VariableDeclaration&>(s), decls,
                          mainBody);
            return;
        case NodeKind::ExportDeclaration:
            emitExport(static_cast<const ExportDeclaration&>(s), decls, defs, mainBody);
            return;
        case NodeKind::ClassDeclaration:
        case NodeKind::TypeAliasDeclaration:
            notImplemented(s, "top-level declaration");
            return;
        case NodeKind::EnumDeclaration:
            // Resolved and emitted as a whole by collectEnums before the emit
            // walk (src/emit/enum.cpp); the statement itself emits nothing.
            return;
        case NodeKind::InterfaceDeclaration:
            // An interface is a structure-only record shape: registered and
            // reified only for `JSON.parse<T>` uses (src/std/json/codegen.cpp);
            // otherwise it stays erased.
            emitJsonInterface(static_cast<const InterfaceDeclaration&>(s));
            return;
        default:
            stmt(s, mainBody, 1);
            return;
    }
}

void Generator::emitExport(const ExportDeclaration& ed, std::string& decls,
                           std::string& defs, std::string& mainBody) {
    if (ed.declaration) {
        emitTopLevel(*ed.declaration, decls, defs, mainBody);
        return;
    }
    if (ed.defaultExpr) {
        emitLineDirective(ed, mainBody);
        line(mainBody, 1, expr(*ed.defaultExpr) + ";");
        return;
    }
    notImplemented(ed, "re-export");
}

// Module-scope `let`/`const`/`var` lowers to a namespace-scope global so that
// functions declared in the unit can read it (a `main`-local cannot be seen by
// them, S1). The declaration moves to the decls section; a non-const
// initializer runs at the statement's original position in `main` (JS order),
// while a const must initialize in place.
void Generator::emitModuleVar(const VariableDeclaration& vd, std::string& decls,
                              std::string& mainBody) {
    const bool isConst = vd.kind == VarKind::Const;
    const std::string cv = isConst ? "const " : "";
    for (const auto& declPtr : vd.declarations) {
        const VariableDeclarator* d = declPtr.get();
        if (!d || !d->id || d->id->kind != NodeKind::Identifier) {
            if (d && d->id) notImplemented(*d->id, "destructuring binding");
            continue;
        }
        const std::string name =
            mapIdent(static_cast<const Identifier&>(*d->id).name);
        reportModuleVarType(*d);
        const std::optional<Type> declared = resolvedBindingType(*d);
        CppInfo ci = declared ? classify(*declared) : CppInfo{};
        if (d->init && !ci.representable) {
            const CppInfo fromInit = info(*d->init);
            if (fromInit.representable) ci = fromInit;
        }
        if (!ci.representable) {
            // No namespace-scope spelling (e.g. an arrow binding, or a
            // top-level `await` initializer that needs the coroutine context):
            // keep the binding a `main`-local, the pre-globals lowering.
            emitLineDirective(*d, mainBody);
            line(mainBody, 1, declaratorText(*d, vd.kind) + ";");
            declare(static_cast<const Identifier&>(*d->id).name,
                    resolvedBindingType(*d));
            continue;
        }
        if (!d->init) {
            emitModuleVarNoInit(ci, name, cv, decls);
            continue;
        }
        // `let a: number[] = Array(3)` / `let a: number[] = []`: the
        // annotation fixes an otherwise unknowable element type
        // (src/std/typed-arrays/codegen.cpp).
        std::string initText = declaratorInitText(*d, ci.type);
        if (initText.empty()) initText = expr(*d->init);
        if (ci.shape == Shape::Optional && isNullishLiteral(*d->init)) {
            initText = "std::nullopt";
        }
        if (isConst && initText.find("co_await") != std::string::npos) {
            // A const initializer that awaits only compiles inside the entry
            // coroutine: keep the binding a `main`-local (no static init).
            emitLineDirective(*d, mainBody);
            line(mainBody, 1, declaratorText(*d, vd.kind) + ";");
            declare(static_cast<const Identifier&>(*d->id).name,
                    resolvedBindingType(*d));
            continue;
        }
        if (isConst) {
            if (isStaticInitExpr(*d->init)) {
                emitLineDirective(*d, decls);
                line(decls, 1, cv + ci.cpp + " " + name + " = " + initText + ";");
            } else {
                // The initializer reads runtime state: evaluate it inside
                // `main` at its source position, after initArgv and the rest
                // of the runtime bootstrap. The binding keeps namespace scope
                // so functions can read it; language-level const-ness is
                // enforced by sema.
                line(decls, 1, ci.cpp + " " + name + "{};");
                emitLineDirective(*d, mainBody);
                line(mainBody, 1, name + " = " + initText + ";");
            }
            continue;
        }
        line(decls, 1, ci.cpp + " " + name + "{};");
        emitLineDirective(*d, mainBody);
        line(mainBody, 1, name + " = " + initText + ";");
    }
}

void Generator::emitModuleVarNoInit(const detail::CppInfo& ci,
                                    const std::string& name,
                                    const std::string& cv, std::string& decls) {
    if (ci.representable && ci.shape != Shape::Void) {
        line(decls, 1, cv + ci.cpp + " " + name + "{};");
    } else {
        line(decls, 1, cv + "auto " + name + " = cp3::runtime::undefined;");
    }
}

// A declared type M1 cannot represent (`number | string`, an object / array /
// class type, ...) keeps the unit "not implemented" instead of being quietly
// mis-typed (same contract as local declarations).
void Generator::reportModuleVarType(const VariableDeclarator& d) {
    if (!d.type) return;
    auto dt = typeOfNode(*d.type);
    if (!dt || classify(*dt).representable || dt->isUnknown() || dt->isError()) {
        return;
    }
    notImplemented(d, "this declared type");
}

void Generator::addFunction(const FunctionDeclaration& fd, std::string& decls,
                            std::string& defs) {
    if (fd.isAsync) {
        // spec §14: `async function f(): T` is a coroutine returning
        // `Promise<T>`; the loop must be driven before the process exits so its
        // continuations (and any rejection report) run.
        needsEventLoop_ = true;
    }
    if (!fd.typeParams.empty()) {
        notImplemented(fd, "generic function");
    }
    const std::string ret = functionReturnType(fd);
    std::vector<std::string> paramsDecl;
    std::vector<std::string> paramsDef;
    paramsDecl.reserve(fd.params.size());
    paramsDef.reserve(fd.params.size());
    for (std::size_t i = 0; i < fd.params.size(); ++i) {
        if (!fd.params[i]) continue;
        paramsDecl.push_back(parameterText(*fd.params[i], fd, i, true));
        paramsDef.push_back(parameterText(*fd.params[i], fd, i, false));
    }
    const std::string head = ret + " " + mapIdent(fd.name) + "(";
    // Default arguments belong on the forward declaration only; repeating
    // them on the definition is a C++ error.
    const std::string signatureDecl = head + joinStrings(paramsDecl, ", ") + ")";
    const std::string signatureDef = head + joinStrings(paramsDef, ", ") + ")";
    line(decls, 1, signatureDecl + ";");

    // int64 fast path (D64, int_mode.cpp): attempt the body once with every
    // `number` lowered to a guarded std::int64_t. A discarded attempt leaves
    // the ordinary double body below as the only lowering; a kept one is
    // emitted next to it, and the wrapper guards the arguments once and calls
    // it, falling through to the ordinary body when a guard bails.
    const bool tryFast = fd.body && intEligible(fd, ret);
    std::string fastBody;
    bool fastKept = false;
    if (tryFast) fastBody = intBody(fd, fastKept);

    const std::string body = plainBody(fd, ret);
    if (fastKept) {
        line(decls, 1, intSignature(fd) + ";");
        emitLineDirective(fd, defs);
        defs += intSignature(fd) + " {\n" + fastBody + "}\n\n";
    }
    const std::string prologue = fastKept ? intWrapperPrologue(fd) : std::string();
    emitLineDirective(fd, defs);
    defs += signatureDef + " {\n";
    // The prologue is structural: pin its lines to the function line so the
    // wrapper's argument stores cannot pose as statements of their own (the
    // source line counter would otherwise drift one line per statement).
    emitMappedRaw(fd, prologue, defs);
    defs += body + "}\n\n";
}

// The ordinary lowering of a function body: cp3 `number` stays a double. This
// is the definition of record (the CP3002 reports happen here, once, whether or
// not an int64 fast path was kept) and the fallback the wrapper re-runs.
std::string Generator::plainBody(const FunctionDeclaration& fd, const std::string& ret) {
    const std::string savedReturn = currentReturn_;
    if (fd.isAsync) {
        // Inside the coroutine `return expr` yields the promise's VALUE type
        // (`co_return expr`), not the C++ signature's `Promise<T>` spelling.
        const CppInfo value = classify(asyncValueType(fd));
        currentReturn_ = (value.representable && value.shape != Shape::Void)
                             ? value.cpp
                             : "void";
    } else {
        currentReturn_ = ret;
    }

    const bool savedInAsync = inAsyncBody_;
    inAsyncBody_ = fd.isAsync;
    ++nestingDepth_;

    std::string body;
    pushScope();
    for (std::size_t i = 0; i < fd.params.size(); ++i) {
        const Parameter& p = *fd.params[i];
        if (p.name && p.name->kind == NodeKind::Identifier) {
            declare(static_cast<const Identifier&>(*p.name).name, parameterType(p, fd, i));
        }
    }
    if (fd.body) {
        stmt(*fd.body, body, 1);
        if (fd.isAsync) {
            // A coroutine must not fall off its end (sema already proved the
            // cp3 body returns on every path); this keeps the C++ valid for the
            // paths a `throw` guards, e.g. a body ending in `throw`.
            line(body, 1, currentReturn_ == "void" ? "co_return;" : "co_return {};");
        }
    } else {
        line(body, 1, "// declaration without a body");
    }
    popScope();

    --nestingDepth_;
    inAsyncBody_ = savedInAsync;
    currentReturn_ = savedReturn;
    return body;
}

std::optional<Type> Generator::parameterType(const Parameter& p,
                                             const FunctionDeclaration& fd,
                                             std::size_t index) const {
    if (p.type) {
        if (auto t = typeOfNode(*p.type)) {
            if (p.optional) return Type::nullable(*t);
            return t;
        }
    }
    auto it = sema_.functions.find(fd.nodeId);
    if (it != sema_.functions.end() && index < it->second.paramTypes.size()) {
        return it->second.paramTypes[index];
    }
    return std::nullopt;
}

std::string Generator::parameterText(const Parameter& p, const FunctionDeclaration& fd,
                                     std::size_t index, bool includeDefault) {
    if (p.isRest) {
        notImplemented(p, "rest parameter");
        return "/* rest */";
    }
    std::string name;
    if (p.name && p.name->kind == NodeKind::Identifier) {
        name = mapIdent(static_cast<const Identifier&>(*p.name).name);
    } else {
        if (p.name) notImplemented(*p.name, "destructuring parameter");
        name = "cp3_param";
    }

    std::string type;
    const std::optional<Type> pt = parameterType(p, fd, index);
    if (pt) {
        CppInfo ci = classify(*pt);
        if (ci.representable) {
            type = ci.cpp;
        }
    }
    if (type.empty()) {
        notImplemented(p, "untyped parameter");
        type = "double";
    }

    std::string out = type + (p.isRef ? "& " : " ") + name;
    if (includeDefault) {
        if (p.defaultValue) {
            out += " = " + expr(*p.defaultValue);
        } else if (p.optional && type.rfind("std::optional<", 0) == 0) {
            // `name?: T` lowers to `std::optional<T> name = std::nullopt` so
            // the argument may be omitted at the call site.
            out += " = std::nullopt";
        }
    }
    return out;
}

std::string Generator::functionReturnType(const FunctionDeclaration& fd) {
    if (fd.isAsync) {
        // An `async function` never returns its annotated value type directly:
        // the C++ signature is the coroutine's promise type (spec §14).
        const CppInfo ci = classify(detail::promiseOf(asyncValueType(fd)));
        if (ci.representable) return ci.cpp;
        notImplemented(fd, "this async function's value type");
        return "void";
    }
    if (fd.returnType) {
        if (auto t = typeOfNode(*fd.returnType)) {
            CppInfo ci = classify(*t);
            if (ci.representable) return ci.cpp;
            // A declared return type M1 cannot represent (e.g. a function
            // type `() => T`, an object/array, a non-primitive union).
            // Report CP3002 so the unit is treated as not-implemented rather
            // than emitting a `void` signature with a dropped `return`.
            notImplemented(fd, "this return type");
        }
    }
    auto it = sema_.functions.find(fd.nodeId);
    if (it != sema_.functions.end()) {
        CppInfo ci = classify(it->second.returnType);
        if (ci.representable) return ci.cpp;
        if (!it->second.returnType.isUnknown()) {
            notImplemented(fd, "this return type");
        }
    }
    // Fallback: infer from the first `return <expr>` in the body.
    if (fd.body) {
        if (const Expr* returned = firstReturnedExpr(*fd.body)) {
            CppInfo ci = info(*returned);
            if (ci.representable) return ci.cpp;
            notImplemented(*returned, "this returned value");
        }
    }
    return "void";
}

// The cp3 value type an `async function` yields (the `Promise<T>`'s `T`).
// An annotation wins (`Promise<T>` or the permissive plain `T`); otherwise the
// sema side table carries it: for an async function `FunctionInfo.returnType`
// is the `Promise<T>` the call yields, resolved after the body was checked.
Type Generator::asyncValueType(const FunctionDeclaration& fd) const {
    if (fd.returnType) {
        if (auto t = typeOfNode(*fd.returnType)) {
            if (detail::isPromiseType(*t)) return detail::promiseValue(*t);
            return *t;
        }
    }
    auto it = sema_.functions.find(fd.nodeId);
    if (it != sema_.functions.end() && detail::isPromiseType(it->second.returnType)) {
        return detail::promiseValue(it->second.returnType);
    }
    return Type::unknown();
}

const Expr* Generator::firstReturnedExpr(const Stmt& s) const {
    if (s.kind == NodeKind::ReturnStatement) {
        const auto& r = static_cast<const ReturnStatement&>(s);
        return r.argument.get();
    }
    if (s.kind == NodeKind::BlockStatement) {
        for (const auto& child : static_cast<const BlockStatement&>(s).body) {
            if (child) {
                if (const Expr* found = firstReturnedExpr(*child)) return found;
            }
        }
    }
    if (s.kind == NodeKind::IfStatement) {
        const auto& is = static_cast<const IfStatement&>(s);
        if (is.thenBranch) {
            if (const Expr* found = firstReturnedExpr(*is.thenBranch)) return found;
        }
        if (is.elseBranch) {
            if (const Expr* found = firstReturnedExpr(*is.elseBranch)) return found;
        }
    }
    return nullptr;
}

}  // namespace cp3
