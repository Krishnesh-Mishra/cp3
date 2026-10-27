// =============================================================================
// src/codegen/src/emit/decl.cpp - codegen declaration/definition lowering
// (STYLE.md §8).
//
// Top-level dispatch, exported declarations, and function emission: forward
// declarations + definitions (default arguments live on the forward declaration
// only) and the definition body. Parameter spelling and return-type resolution
// live in emit/patterns.cpp and this unit; `async function` bodies lower to C++20
// coroutines whose promise type is the runtime's `Promise<T>` (spec §14).
// A form with no C++ spelling is a hard type error, never a skipped program.
// =============================================================================
#include "detail/generator/generator.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "detail/generator/cpp_writer.h"
#include "detail/generator/family.h"
#include "detail/generator/mangle.h"
#include "detail/expr/pattern_emitter.h"
#include "detail/generator/type_map.h"
#include "cp3/diag/diagnostic_codes.hpp"

namespace cp3 {

using namespace detail;

namespace {

// A tuple's C++ spelling when every element has one. `classify` spells only
// all-primitive tuples; a tuple holding a record or class handle is still a
// value `std::tuple` can carry, so the declaration/signature sites use this
// wider form directly.
std::optional<std::string> tupleSpelling(const Type& t) {
    std::string cpp;
    if (!isTupleType(t) || !tupleCpp(t, cpp)) return std::nullopt;
    return cpp;
}

}  // namespace

// Records every function declared in this unit (including `export function`)
// and every local binding introduced by an import, so a builtin call/await
// rewrite can be checked against the names the unit actually declares.
void Generator::collectDeclaredNames(const Stmt& s) {
    if (s.kind == NodeKind::FunctionDeclaration) {
        const auto& fd = static_cast<const FunctionDeclaration&>(s);
        functions_.insert(fd.name);
        functionDecls_[fd.name] = &fd;
        if (fd.isAsync) {
            asyncFunctions_.insert(fd.name);
        }
    } else if (s.kind == NodeKind::ImportDeclaration) {
        const auto& imp = static_cast<const ImportDeclaration&>(s);
        auto note = [this](const std::string& name) {
            if (!name.empty()) importBindings_.insert(name);
        };
        note(imp.defaultBinding);
        note(imp.namespaceBinding);
        note(imp.alias);
        for (const auto& specifier : imp.specifiers) {
            if (!specifier) continue;
            note(specifier->local.empty() ? specifier->imported : specifier->local);
        }
    } else if (s.kind == NodeKind::ExportDeclaration) {
        const auto& ed = static_cast<const ExportDeclaration&>(s);
        if (ed.declaration) collectDeclaredNames(*ed.declaration);
    }
}

// The const-reference spelling for a proven array-read binding. The proof
// (support/array_refs.cpp) is per declarator, so only this spelling changes;
// the binding type the emitter records stays the element type.
//
// The emitted text must match the ordinary `arrayAt` spelling exactly, except
// that the element is bound by reference: the same bounds check and panic, the
// same index operand text (an enum index keeps its `static_cast<double>`),
// and the same element type. Every check here is a guard that fails closed to
// the copying spelling.
std::string Generator::arrayRefDeclText(const VariableDeclarator& d,
                                        const std::string& name) {
    if (arrayRefDecls_.count(d.nodeId) == 0) return std::string();
    if (!d.init || d.init->kind != NodeKind::IndexExpr) return std::string();
    // A declared type belongs to the ordinary lowering (it carries the
    // not-implemented report for a type M1 cannot spell).
    if (d.type) return std::string();
    const auto& ix = static_cast<const IndexExpr&>(*d.init);
    if (!ix.object || !ix.index) return std::string();
    const Type arr = effectiveType(*ix.object);
    CppInfo ec;
    if (arr.kind != TypeKind::Array ||
        !arrayElementInfo(arrayElemOf(arr), ec) || !ec.representable) {
        return std::string();
    }
    if (ec.shape != Shape::Number && ec.shape != Shape::String &&
        ec.shape != Shape::Bool && ec.shape != Shape::Enum) {
        return std::string();
    }
    // The binding's own type must be the element type: a binding the
    // ordinary lowering spells differently (an optional, a cell) keeps the
    // copying form.
    const CppInfo ci = bindingCpp(d);
    if (!ci.representable || ci.cpp != ec.cpp || ci.shape != ec.shape) {
        return std::string();
    }
    // A shared cell is read through `get()` and written through `set()`; a
    // reference binding is neither.
    if (isCell(name)) return std::string();
    needsTypedArray_ = true;
    return "const " + ec.cpp + "& " + name +
           " = ::cp3::runtime::arrayAtRef<" + ec.cpp + ">(" +
           expr(*ix.object) + ", " + indexOperandText(*ix.index) + ")";
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
            // A class declaration (spec 15) is registered up front and emits
            // its struct in `emitAllClasses` (after the walk).
            emitTopLevelClass(static_cast<const ClassDeclaration&>(s));
            return;
        case NodeKind::TypeAliasDeclaration:
            // A type alias is a compile-time name for a type: it emits no C++
            // of its own. Every use site carries the resolved type (sema
            // publishes it in the node-type table), so the alias itself is
            // erased - like an interface declaration.
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

// A function-local enum (spec 04): the C++ `enum class` cannot live inside a
// function, so the declaration is namespace scoped - the same shape as a
// top-level enum, whose text `collectEnums` (emit/enum.cpp) emits. The
// statement itself emits nothing. A name that is already taken cannot have a
// second namespace-scoped declaration: that is a duplicate.
void Generator::emitLocalEnum(const Stmt& s) {
    if (intMode_) {
        // The int64 fast-path attempt emits the body twice (the fast one and
        // the ordinary fallback): fail the attempt and let the ordinary pass
        // declare the enum once.
        intFail();
        return;
    }
    const auto& e = static_cast<const EnumDeclaration&>(s);
    if (!e.name.empty() && enums_.count(e.name) != 0) {
        reportCodegenError(s, diagcode::kDuplicateDeclaration,
                           "duplicate enum name '" + e.name +
                               "' (the enum declarations of a unit share one "
                               "namespace scope)");
        return;
    }
    collectEnum(&e);
}

void Generator::emitExport(const ExportDeclaration& ed, std::string& decls,
                           std::string& defs, std::string& mainBody) {
    if (ed.declaration) {
        emitTopLevel(*ed.declaration, decls, defs, mainBody);
        return;
    }
    if (ed.defaultExpr) {
        // A whole-program build: an identifier default export is exactly the
        // named binding the link table already exposes under its qualified
        // C++ name. An anonymous value (arrow/object literal) has no C++ name
        // to bind either; a use reports the link miss, so the declaration
        // itself stays quiet.
        if (programMode_) {
            const NodeKind kind = ed.defaultExpr->kind;
            if (kind == NodeKind::Identifier || kind == NodeKind::ArrowFunction ||
                kind == NodeKind::ObjectLiteral) {
                return;
            }
        }
        emitLineDirective(ed, mainBody);
        line(mainBody, 1, expr(*ed.defaultExpr) + ";");
        return;
    }
    // A module-link export (`export { a }`, `export { a } from "m"`,
    // `export * from "m"`): the export surface belongs to the module loader
    // (src/modules) and, in a whole-program build, to the link table
    // (detail/program_link.h), which resolves every exported name to the
    // qualified C++ name of the unit that owns it. The C++ side has nothing
    // to add: the names a list exports are already declared in this unit's
    // namespace, and a re-exported name is reached through the link table.
    return;
}


void Generator::addFunction(const FunctionDeclaration& fd, std::string& decls,
                            std::string& defs) {
    if (fd.isAsync) {
        // spec §14: `async function f(): T` is a coroutine returning
        // `Promise<T>`; the loop must be driven before the process exits so its
        // continuations (and any rejection report) run.
        needsEventLoop_ = true;
    }
    // Real C++ function templates (spec §16): every signature line carries the
    // `template <typename T, ...>` prefix (support/generic.cpp).
    const std::string prefix = templatePrefix(fd);
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
    const std::string signatureDecl = prefix + head + joinStrings(paramsDecl, ", ") + ")";
    const std::string signatureDef = prefix + head + joinStrings(paramsDef, ", ") + ")";
    line(decls, 1, signatureDecl + ";");

    // int64 fast path (D64, int_mode.cpp): attempt the body once with every
    // `number` lowered to a guarded std::int64_t. A discarded attempt leaves
    // the ordinary double body below as the only lowering; a kept one is
    // emitted next to it, and the wrapper guards the arguments once and calls
    // it, falling through to the ordinary body when a guard bails.
    const bool tryFast = fd.typeParams.empty() && fd.body && intEligible(fd, ret);
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
// is the definition of record (the signature/type reports happen here, once,
// whether or not an int64 fast path was kept) and the fallback the wrapper
// re-runs.
std::string Generator::plainBody(const FunctionDeclaration& fd, const std::string& ret) {
    const std::string savedReturn = currentReturn_;
    const std::optional<Type> savedReturnType = currentReturnType_;
    if (fd.isAsync) {
        // Inside the coroutine `return expr` yields the promise's VALUE type
        // (`co_return expr`), not the C++ signature's `Promise<T>` spelling.
        const Type value = asyncValueType(fd);
        const CppInfo ci = classify(value);
        if (const auto tuple = tupleSpelling(value)) {
            currentReturn_ = *tuple;
        } else {
            currentReturn_ = (ci.representable && ci.shape != Shape::Void)
                                 ? ci.cpp
                                 : "void";
        }
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
    const bool savedTuple = currentReturnTuple_;
    // A tuple-returning body lowers `return [a, b]` to `std::make_tuple`
    // (support/generic.cpp).
    currentReturnTuple_ = currentReturn_.rfind("std::tuple<", 0) == 0;
    ++nestingDepth_;

    std::string body;
    pushScope();
    // A generic function's type parameters are names in its body scope; the
    // parameter/return spellings resolved above already use them (spec §16).
    for (const auto& tp : fd.typeParams) {
        if (!tp) continue;
        declare(mapIdent(tp->name),
                Type::named(TypeKind::TypeParam, tp->name));
    }
    for (std::size_t i = 0; i < fd.params.size(); ++i) {
        const Parameter& p = *fd.params[i];
        if (const Identifier* id = parameterIdentifier(p)) {
            declare(id->name, parameterType(p, fd, i));
        }
    }
    // A destructured parameter unpacks in the prologue; the C++ parameter is
    // the generated handle `parameterText` spelled (emit/patterns.cpp).
    emitPatternParams(fd, body, 1);
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
    currentReturnTuple_ = savedTuple;
    currentReturn_ = savedReturn;
    currentReturnType_ = savedReturnType;
    return body;
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
        // the C++ signature is the coroutine's promise type (spec §14). A tuple
        // value is spellable even when an element is a record or class handle.
        const Type value = asyncValueType(fd);
        if (const auto tuple = tupleSpelling(value)) {
            return "::cp3::runtime::Promise<" + *tuple + ">";
        }
        const CppInfo ci = classify(detail::promiseOf(value));
        if (ci.representable) return ci.cpp;
        reportUnsupported(fd, diagcode::kTypeMismatch,
                          "type mismatch: this async function's value type has "
                          "no C++ spelling");
        return "void";
    }
    // A tuple return type has a `std::tuple` spelling regardless of whether its
    // elements are primitive, so resolve it before the general value-type
    // fallback (which may otherwise pick a narrower returned expression type).
    if (fd.returnType) {
        if (auto t = typeOfNode(*fd.returnType)) {
            if (const auto tuple = tupleSpelling(*t)) return *tuple;
            if (isTupleType(*t)) {
                reportUnsupported(fd, diagcode::kTypeMismatch,
                                  "type mismatch: this return type has no C++ "
                                  "spelling");
                return "void";
            }
        }
    }
    auto signature = sema_.functions.find(fd.nodeId);
    if (signature != sema_.functions.end()) {
        if (const auto tuple = tupleSpelling(signature->second.returnType)) {
            return *tuple;
        }
    }
    // A generic function's annotation names its own type parameters (`T`, `T[]`,
    // `[A, B]`); the resolved signature is the only place those are known
    // (spec §16).
    if (!fd.typeParams.empty()) {
        if (signature != sema_.functions.end()) {
            const CppInfo ci = classify(signature->second.returnType);
            if (ci.representable) return ci.cpp;
        }
    }
    if (auto value = returnValueType(fd)) {
        const CppInfo ci = classify(*value);
        if (ci.representable) return ci.cpp;
        if (!value->isUnknown()) {
            reportUnsupported(fd, diagcode::kTypeMismatch,
                              "type mismatch: this return type has no C++ "
                              "spelling");
            return "void";
        }
    }
    // An unannotated function whose only return value has an unknown type
    // (`function f(x) { return x; }`, an untyped parameter) has no cp3 type to
    // spell, but C++ can deduce it. A function with no returned value stays
    // `void`.
    if (fd.body && firstReturnedExpr(*fd.body)) return "auto";
    return "void";
}

// The cp3 value type an `async function` yields (the `Promise<T>`'s `T`).
// An annotation wins (`Promise<T>` or the permissive plain `T`); otherwise the
// sema side table carries it: for an async function `FunctionInfo.returnType`
// is the `Promise<T>` the call yields, resolved after the body was checked.
Type Generator::asyncValueType(const FunctionDeclaration& fd) const {
    if (fd.returnType) {
        if (auto t = typeOfNode(*fd.returnType)) {
            const Type value = detail::isPromiseType(*t)
                                   ? detail::promiseValue(*t)
                                   : *t;
            if (!value.isUnknown() && !value.isError()) return value;
        }
    }
    auto it = sema_.functions.find(fd.nodeId);
    if (it != sema_.functions.end() && detail::isPromiseType(it->second.returnType)) {
        const Type value = detail::promiseValue(it->second.returnType);
        if (!value.isUnknown() && !value.isError()) return value;
    }
    // An `unknown` annotation has no C++ spelling, but the first returned
    // expression still fixes the promise's carried value (the same concrete
    // deduction an unannotated synchronous function gets).
    if (fd.body) {
        if (const Expr* returned = firstReturnedExpr(*fd.body)) {
            const Type t = effectiveType(*returned);
            if (!t.isUnknown() && !t.isError()) return t;
        }
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
