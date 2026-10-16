// =============================================================================
// src/codegen/src/emit_decl.cpp — codegen declaration/definition lowering
// (STYLE.md §8).
//
// Top-level dispatch, exported declarations, and function emission: forward
// declarations + definitions (default arguments live on the forward declaration
// only), parameter type spelling, and return-type resolution (annotated, sema,
// then the first `return` in the body). Out-of-scope declarations report CP3002.
// =============================================================================
#include "generator.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "cpp_writer.h"
#include "mangle.h"
#include "type_map.h"

namespace cp3 {

using namespace detail;

// Records every function declared in this unit (including `export function`)
// so a call can be checked against them.
void Generator::collectFunctionNames(const Stmt& s) {
    if (s.kind == NodeKind::FunctionDeclaration) {
        functions_.insert(static_cast<const FunctionDeclaration&>(s).name);
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
        case NodeKind::ExportDeclaration:
            emitExport(static_cast<const ExportDeclaration&>(s), decls, defs, mainBody);
            return;
        case NodeKind::ClassDeclaration:
        case NodeKind::InterfaceDeclaration:
        case NodeKind::EnumDeclaration:
        case NodeKind::TypeAliasDeclaration:
            notImplemented(s, "top-level declaration");
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
        line(mainBody, 1, expr(*ed.defaultExpr) + ";");
        return;
    }
    notImplemented(ed, "re-export");
}

void Generator::addFunction(const FunctionDeclaration& fd, std::string& decls,
                            std::string& defs) {
    if (fd.isAsync) {
        notImplemented(fd, "async function");
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

    const std::string savedReturn = currentReturn_;
    currentReturn_ = ret;

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
    } else {
        line(body, 1, "// declaration without a body");
    }
    popScope();

    currentReturn_ = savedReturn;

    defs += signatureDef + " {\n" + body + "}\n\n";
}

std::optional<Type> Generator::parameterType(const Parameter& p,
                                             const FunctionDeclaration& fd,
                                             std::size_t index) const {
    if (p.type) {
        if (auto t = typeFromTypeNode(*p.type)) {
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
    if (fd.returnType) {
        if (auto t = typeFromTypeNode(*fd.returnType)) {
            CppInfo ci = classify(*t);
            if (ci.representable) return ci.cpp;
            // A declared return type M1 cannot represent (e.g. a function
            // type `() => T`, an object/array, a non-primitive union).
            // Report CP3002 so the unit is treated as not-implemented rather
            // than emitting a `void` signature with a dropped `return`.
            notImplemented(fd, "return type outside M1");
        }
    }
    auto it = sema_.functions.find(fd.nodeId);
    if (it != sema_.functions.end()) {
        CppInfo ci = classify(it->second.returnType);
        if (ci.representable) return ci.cpp;
        if (!it->second.returnType.isUnknown()) {
            notImplemented(fd, "return type outside M1");
        }
    }
    // Fallback: infer from the first `return <expr>` in the body.
    if (fd.body) {
        if (const Expr* returned = firstReturnedExpr(*fd.body)) {
            CppInfo ci = info(*returned);
            if (ci.representable) return ci.cpp;
            notImplemented(*returned, "returned value outside M1");
        }
    }
    return "void";
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
