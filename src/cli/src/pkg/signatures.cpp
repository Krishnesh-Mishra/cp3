// =============================================================================
// src/cli/src/pkg/signatures.cpp - `index.cp3` generation DEFINITIONS.
//
// OWNERSHIP: cli module (src/cli/**). Implements detail/signatures.hpp.
//
// The rewrite is source-span based: declarations are copied from the original
// text with their bodies cut out at the body's own span, so parameter names,
// annotations, generic parameters and formatting survive exactly as written.
// It is deliberately conservative - anything the walk does not recognize is
// either copied verbatim (declarations) or dropped (runtime statements).
// =============================================================================
#include "detail/signatures.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace cp3::cli {

namespace {

std::string trimmed(std::string_view text) {
    while (!text.empty() &&
           (text.front() == ' ' || text.front() == '\t' || text.front() == '\n' ||
            text.front() == '\r')) {
        text.remove_prefix(1);
    }
    while (!text.empty() &&
           (text.back() == ' ' || text.back() == '\t' || text.back() == '\n' ||
            text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return std::string(text);
}

// Declaration text through the start of its body, closed with `;` - i.e. the
// signature. Declarations without a body are copied as written.
std::string withoutBody(const SourceFile& file, const Node& decl,
                        const Node* body) {
    if (body == nullptr || body->span.begin.offset <= decl.span.begin.offset) {
        return trimmed(file.slice(decl.span)) + ";";
    }
    const SourceSpan head = SourceManager::makeSpan(
        &file, decl.span.begin.offset, body->span.begin.offset);
    return trimmed(file.slice(head)) + ";";
}

// A class with every method/constructor/accessor body elided.
std::string stripClass(const SourceFile& file, const ClassDeclaration& cls) {
    if (cls.members.empty()) return trimmed(file.slice(cls.span));
    std::string out;
    const SourceSpan head = SourceManager::makeSpan(
        &file, cls.span.begin.offset, cls.members.front()->span.begin.offset);
    out += trimmed(file.slice(head));
    for (const ClassMemberPtr& member : cls.members) {
        out += "\n  ";
        switch (member->kind) {
            case NodeKind::MethodDeclaration: {
                const auto& method =
                    static_cast<const MethodDeclaration&>(*member);
                out += withoutBody(file, method, method.body.get());
                break;
            }
            case NodeKind::ConstructorDeclaration: {
                const auto& ctor =
                    static_cast<const ConstructorDeclaration&>(*member);
                out += withoutBody(file, ctor, ctor.body.get());
                break;
            }
            case NodeKind::AccessorDeclaration: {
                const auto& accessor =
                    static_cast<const AccessorDeclaration&>(*member);
                out += withoutBody(file, accessor, accessor.body.get());
                break;
            }
            default:
                out += trimmed(file.slice(member->span));
                break;
        }
    }
    out += "\n}";
    return out;
}

std::string stripStatement(const SourceFile& file, const Stmt& stmt) {
    switch (stmt.kind) {
        case NodeKind::FunctionDeclaration: {
            const auto& fn = static_cast<const FunctionDeclaration&>(stmt);
            return (fn.isExported ? std::string("export ") : std::string()) +
                   withoutBody(file, fn, fn.body.get());
        }
        case NodeKind::ClassDeclaration: {
            const auto& cls = static_cast<const ClassDeclaration&>(stmt);
            return (cls.isExported ? std::string("export ") : std::string()) +
                   stripClass(file, cls);
        }
        case NodeKind::ExportDeclaration: {
            const auto& exp = static_cast<const ExportDeclaration&>(stmt);
            if (exp.kind == ExportKind::Declaration && exp.declaration) {
                return "export " + stripStatement(file, *exp.declaration);
            }
            // `export default { ... }`, `export { a, b }` and re-exports are
            // part of the surface exactly as written.
            return trimmed(file.slice(stmt.span));
        }
        case NodeKind::VariableDeclaration:
        case NodeKind::ImportDeclaration:
        case NodeKind::InterfaceDeclaration:
        case NodeKind::EnumDeclaration:
        case NodeKind::TypeAliasDeclaration:
            return trimmed(file.slice(stmt.span));
        default:
            // Runtime statements (control flow, calls) are not surface.
            return {};
    }
}

}  // namespace

bool hasDefaultExport(const Program& program) {
    for (const StmtPtr& stmt : program.body) {
        if (stmt->kind != NodeKind::ExportDeclaration) continue;
        const auto& exp = static_cast<const ExportDeclaration&>(*stmt);
        if (exp.kind == ExportKind::Default) return true;
    }
    return false;
}

std::string stripToSignatures(const SourceFile& file, const Program& program) {
    std::string out;
    out += "// index.cp3 - generated by cp3c from ";
    out += std::filesystem::path(file.path).filename().string();
    out += "; exported signatures and types only.\n";
    out += "// Do not edit: `cp3c build` regenerates this file for the .cp3pak.\n";
    for (const StmtPtr& stmt : program.body) {
        const std::string text = stripStatement(file, *stmt);
        if (text.empty()) continue;
        out += "\n";
        out += text;
        out += "\n";
    }
    return out;
}

}  // namespace cp3::cli
