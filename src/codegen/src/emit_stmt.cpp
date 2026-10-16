// =============================================================================
// src/codegen/src/emit_stmt.cpp — codegen statement lowering (STYLE.md §8).
//
// Statements, blocks and control flow: variable declarations (including the
// M1 type spelling / nullish folding), if/while/do/for, return (with the
// void-vs-valued signature check), break/continue. Out-of-scope statements
// report CP3002 and emit a compilable stub.
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

void Generator::stmt(const Stmt& s, std::string& out, int indent) {
    switch (s.kind) {
        case NodeKind::BlockStatement: {
            pushScope();
            line(out, indent, "{");
            for (const auto& child : static_cast<const BlockStatement&>(s).body) {
                if (child) stmt(*child, out, indent + 1);
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
            if (es.expression) {
                line(out, indent, expr(*es.expression) + ";");
            } else {
                line(out, indent, ";");
            }
            return;
        }
        case NodeKind::VariableDeclaration:
            emitVarDecl(static_cast<const VariableDeclaration&>(s), out, indent);
            return;
        case NodeKind::IfStatement: {
            const auto& is = static_cast<const IfStatement&>(s);
            const std::string condition = is.condition ? cond(*is.condition) : "false";
            line(out, indent, "if (" + condition + ") {");
            if (is.thenBranch) emitBranch(*is.thenBranch, out, indent + 1);
            if (is.elseBranch) {
                line(out, indent, "} else {");
                emitBranch(*is.elseBranch, out, indent + 1);
            }
            line(out, indent, "}");
            return;
        }
        case NodeKind::WhileStatement: {
            const auto& ws = static_cast<const WhileStatement&>(s);
            const std::string condition = ws.condition ? cond(*ws.condition) : "false";
            line(out, indent, "while (" + condition + ") {");
            if (ws.body) emitBranch(*ws.body, out, indent + 1);
            line(out, indent, "}");
            return;
        }
        case NodeKind::DoWhileStatement: {
            const auto& dw = static_cast<const DoWhileStatement&>(s);
            line(out, indent, "do {");
            if (dw.body) emitBranch(*dw.body, out, indent + 1);
            const std::string condition = dw.condition ? cond(*dw.condition) : "false";
            line(out, indent, "} while (" + condition + ");");
            return;
        }
        case NodeKind::ForStatement: {
            emitFor(static_cast<const ForStatement&>(s), out, indent);
            return;
        }
        case NodeKind::ReturnStatement: {
            emitReturn(static_cast<const ReturnStatement&>(s), out, indent);
            return;
        }
        case NodeKind::BreakStatement:
            line(out, indent, "break;");
            return;
        case NodeKind::ContinueStatement:
            line(out, indent, "continue;");
            return;
        default:
            notImplemented(s, "statement");
            line(out, indent, "/* CP3002: unsupported statement */");
            return;
    }
}

void Generator::emitBranch(const Stmt& s, std::string& out, int indent) {
    if (s.kind == NodeKind::BlockStatement) {
        for (const auto& child : static_cast<const BlockStatement&>(s).body) {
            if (child) stmt(*child, out, indent);
        }
    } else {
        stmt(s, out, indent);
    }
}

void Generator::emitVarDecl(const VariableDeclaration& vd, std::string& out, int indent) {
    for (const auto& decl : vd.declarations) {
        if (!decl) continue;
        std::string text = declaratorText(*decl, vd.kind);
        line(out, indent, text + ";");
        if (decl->id && decl->id->kind == NodeKind::Identifier) {
            declare(static_cast<const Identifier&>(*decl->id).name, resolvedBindingType(*decl));
        }
    }
}

std::string Generator::declaratorText(const VariableDeclarator& d, VarKind kind) {
    if (!d.id || d.id->kind != NodeKind::Identifier) {
        if (d.id) notImplemented(*d.id, "destructuring binding");
        return "auto cp3_error = 0.0";
    }
    const std::string name = mapIdent(static_cast<const Identifier&>(*d.id).name);
    const std::string cv = (kind == VarKind::Const) ? "const " : "";
    // A declared type M1 cannot represent (`number | string`, an object /
    // array / class type, ...) is out of scope: report it so the unit is
    // treated as not-implemented instead of being quietly mis-typed.
    if (d.type) {
        if (auto dt = typeFromTypeNode(*d.type)) {
            if (!classify(*dt).representable && !dt->isUnknown() && !dt->isError()) {
                notImplemented(d, "declared type outside M1");
            }
        }
    }
    const std::optional<Type> declared = resolvedBindingType(d);

    if (d.init) {
        CppInfo ci = declared ? classify(*declared) : CppInfo{};
        if (!ci.representable) {
            CppInfo fromInit = info(*d.init);
            if (fromInit.representable) {
                ci = fromInit;
            }
        }
        std::string typeSpelling = ci.representable ? ci.cpp : "auto";
        std::string initText;
        if (ci.shape == Shape::Optional && isNullishLiteral(*d.init)) {
            initText = "std::nullopt";
        } else {
            initText = expr(*d.init);
        }
        return cv + typeSpelling + " " + name + " = " + initText;
    }

    CppInfo ci = declared ? classify(*declared) : CppInfo{};
    if (ci.shape == Shape::Optional) {
        return cv + ci.cpp + " " + name;
    }
    if (ci.representable && ci.shape != Shape::Void) {
        return cv + ci.cpp + " " + name + "{}";
    }
    return cv + "auto " + name + " = cp3::runtime::undefined";
}

void Generator::emitFor(const ForStatement& f, std::string& out, int indent) {
    pushScope();
    std::string init;
    if (f.init) {
        if (f.init->kind == NodeKind::VariableDeclaration) {
            const auto& vd = static_cast<const VariableDeclaration&>(*f.init);
            std::vector<std::string> parts;
            for (const auto& d : vd.declarations) {
                if (d) parts.push_back(declaratorText(*d, vd.kind));
            }
            init = joinStrings(parts, ", ");
            for (const auto& d : vd.declarations) {
                if (d && d->id && d->id->kind == NodeKind::Identifier) {
                    declare(static_cast<const Identifier&>(*d->id).name,
                            resolvedBindingType(*d));
                }
            }
        } else if (auto exp = dynamic_cast<const Expr*>(f.init.get())) {
            init = expr(*exp);
        }
    }
    const std::string condition = f.condition ? cond(*f.condition) : "";
    const std::string update = f.update ? expr(*f.update) : "";
    line(out, indent, "for (" + init + "; " + condition + "; " + update + ") {");
    if (f.body) emitBranch(*f.body, out, indent + 1);
    line(out, indent, "}");
    popScope();
}

void Generator::emitReturn(const ReturnStatement& r, std::string& out, int indent) {
    if (r.argument) {
        // Always lower the returned expression so its own out-of-scope
        // parts report CP3002 instead of being silently dropped.
        const std::string value = expr(*r.argument);
        if (currentReturn_ == "void") {
            // The enclosing signature is `void` either because the cp3
            // return type is out of M1 scope or the function is genuinely
            // void. Either way a value is being returned, so flag it and
            // keep the emitted C++ valid.
            notImplemented(r, "returning a value from a void/unrepresentable signature");
            line(out, indent, "(void)(" + value + ");");
            line(out, indent, "return;");
            return;
        }
        line(out, indent, "return " + value + ";");
        return;
    }
    if (currentReturn_ == "void") {
        line(out, indent, "return;");
    } else {
        line(out, indent, "return {};");
    }
}

}  // namespace cp3
