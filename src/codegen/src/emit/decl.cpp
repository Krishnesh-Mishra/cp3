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
#include "detail/pattern_emitter.h"
#include "detail/type_map.h"

namespace cp3 {

using namespace detail;

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
    const std::optional<Type> savedReturnType = currentReturnType_;
    if (fd.isAsync) {
        // Inside the coroutine `return expr` yields the promise's VALUE type
        // (`co_return expr`), not the C++ signature's `Promise<T>` spelling.
        const Type value = asyncValueType(fd);
        const CppInfo ci = classify(value);
        currentReturn_ = (ci.representable && ci.shape != Shape::Void)
                             ? ci.cpp
                             : "void";
        currentReturnType_ = value;
    } else {
        currentReturn_ = ret;
        currentReturnType_ = returnValueType(fd);
    }

    const std::optional<Type> savedExpected = expected_;
    expected_ = currentReturnType_;
    const bool savedInAsync = inAsyncBody_;
    inAsyncBody_ = fd.isAsync;
    const std::uint32_t savedFrame = currentFrameId_;
    currentFrameId_ = fd.nodeId;
    ++nestingDepth_;

    std::string body;
    pushScope();
    for (std::size_t i = 0; i < fd.params.size(); ++i) {
        const Parameter& p = *fd.params[i];
        if (const Identifier* id = parameterIdentifier(p)) {
            declare(id->name, parameterType(p, fd, i));
        } else if (p.name) {
            // A destructuring parameter binds its names at the top of the
            // body (below); the parameter itself is the `cp3_param<i>` local.
            PatternEmitter(*this).emitParam(p, fd, i, body);
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
    currentFrameId_ = savedFrame;
    inAsyncBody_ = savedInAsync;
    expected_ = savedExpected;
    currentReturn_ = savedReturn;
    currentReturnType_ = savedReturnType;
    return body;
}

std::optional<Type> Generator::parameterType(const Parameter& p,
                                             const FunctionDeclaration& fd,
                                             std::size_t index) const {
    std::optional<Type> type;
    if (p.type) {
        if (auto t = typeOfNode(*p.type)) type = t;
    }
    if (!type) {
        auto it = sema_.functions.find(fd.nodeId);
        if (it != sema_.functions.end() && index < it->second.paramTypes.size()) {
            type = it->second.paramTypes[index];
        }
    }
    if (!type) return std::nullopt;
    if (p.isRest) {
        // The rest parameter's cp3 type is the packed array: an annotation
        // already spells `E[]`, whereas sema publishes the element `E`.
        if (type->kind != TypeKind::Array) type = Type::array(*type);
        return type;
    }
    if (p.optional && !type->isNullable() && !type->isNullish()) {
        type = Type::nullable(*type);
    }
    return type;
}

std::string Generator::parameterText(const Parameter& p, const FunctionDeclaration& fd,
                                     std::size_t index, bool includeDefault) {
    std::string name;
    if (const Identifier* id = parameterIdentifier(p)) {
        name = mapIdent(id->name);
    } else {
        // A destructuring parameter has no C++ name of its own: the bindings
        // are emitted at the top of the body from the `cp3_param<index>` local
        // (emitPatternParam).
        name = "cp3_param" + std::to_string(index);
    }

    std::string type;
    const std::optional<Type> pt = parameterType(p, fd, index);
    if (pt) {
        CppInfo ci = classify(*pt);
        if (ci.representable && ci.shape != Shape::Void) {
            type = ci.cpp;
        }
    }
    if (type.empty()) {
        notImplemented(p, p.isRest ? "this rest parameter" : "untyped parameter");
        type = p.isRest ? "::cp3::runtime::Array<double>" : "double";
    }

    std::string out = type + (p.isRef ? "& " : " ") + name;
    if (includeDefault) {
        if (p.defaultValue) {
            const std::optional<Type> savedExpected = expected_;
            expected_ = pt;
            out += " = " + expr(*p.defaultValue);
            expected_ = savedExpected;
        } else if (p.optional && type.rfind("std::optional<", 0) == 0) {
            // `name?: T` lowers to `std::optional<T> name = std::nullopt` so
            // the argument may be omitted at the call site.
            out += " = std::nullopt";
        }
    }
    return out;
}

// The cp3 type of the value a function returns: the annotation first, then
// sema's resolved signature, then the first `return` in the body. The first
// candidate with a C++ spelling wins, so a nominal annotation sema resolved to
// a structural label still lowers by its shape; a function whose candidates
// are all unrepresentable keeps the first one (the caller reports it).
std::optional<Type> Generator::returnValueType(const FunctionDeclaration& fd) const {
    std::vector<Type> candidates;
    if (fd.returnType) {
        if (auto t = typeOfNode(*fd.returnType)) candidates.push_back(*t);
    }
    auto it = sema_.functions.find(fd.nodeId);
    if (it != sema_.functions.end()) {
        const Type& t = it->second.returnType;
        if (!t.isUnknown() && !t.isError()) candidates.push_back(t);
    }
    if (fd.body) {
        if (const Expr* returned = firstReturnedExpr(*fd.body)) {
            const Type t = effectiveType(*returned);
            if (!t.isUnknown() && !t.isError()) candidates.push_back(t);
        }
    }
    if (candidates.empty()) return std::nullopt;
    for (const Type& t : candidates) {
        if (classify(t).representable) return t;
    }
    return candidates.front();
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
    if (auto value = returnValueType(fd)) {
        const CppInfo ci = classify(*value);
        if (ci.representable) return ci.cpp;
        if (!value->isUnknown()) {
            notImplemented(fd, "this return type");
        }
        return "void";
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
