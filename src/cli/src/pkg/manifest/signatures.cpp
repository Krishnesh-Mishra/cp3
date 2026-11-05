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
#include "detail/pkg/signatures.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "cp3/host/native_types.hpp"
#include "cp3/host/options.hpp"

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

// -----------------------------------------------------------------------------
// Package interop (Rule 3): a `.cp3pak`'s `index.cp3` is what a consumer's
// front end reads to type-check imports. Its type annotations must therefore
// resolve the same way in every consumer project, regardless of the
// consumer's `typeSystem`. An unprefixed native spelling (`i64`, `long long`)
// resolves only under the author's vocabulary, so every native type name this
// file copies is rewritten to a prefixed spelling (`r_'i64'`, `c_'long long'`),
// which resolves identically in every project. TypeScript-authored packages
// have no native spellings, so they copy verbatim.
// -----------------------------------------------------------------------------
struct TypeNameRename {
    SourceSpan span;   // the name-only portion of the NamedType
    std::string text;  // the prefixed spelling to emit
};

struct Normalizer {
    const SourceFile& file;
    TypeSystem typeSystem;
    std::vector<TypeNameRename> renames;

    Normalizer(const SourceFile& f, TypeSystem ts)
        : file(f), typeSystem(ts) {}

    bool active() const { return typeSystem != TypeSystem::TypeScript; }

    void note(const NamedType& named) {
        if (!active()) return;
        const NativeNameResult resolved =
            resolvePlainNativeTypeName(typeSystem, named.name);
        if (!resolved.type) return;  // ordinary types are fine as written.
        const std::size_t begin = named.span.begin.offset;
        const std::size_t length = named.name.size();
        if (begin + length > named.span.end.offset) return;
        // The name span is the prefix of the NamedType's own span. (The
        // parser folds qualified/multi-word spellings, e.g. `std::size_t` or
        // `unsigned long`, into one NamedType spanning the whole annotation.)
        const SourceSpan nameSpan =
            SourceManager::makeSpan(&file, begin, begin + length);
        if (file.slice(nameSpan) != named.name) {
            return;  // folded spelling that does not match the source bytes
        }
        renames.push_back({nameSpan,
                           std::string(typeSystem == TypeSystem::Rust ? "r_'"
                                                                     : "c_'") +
                               named.name + "'"});
    }

    void sort() {
        std::stable_sort(renames.begin(), renames.end(),
                         [](const TypeNameRename& a,
                            const TypeNameRename& b) {
                             return a.span.begin.offset < b.span.begin.offset;
                         });
    }

    std::string slice(const SourceSpan& span) const {
        if (renames.empty()) return std::string(file.slice(span));
        std::string out;
        std::size_t cursor = span.begin.offset;
        for (const TypeNameRename& rename : renames) {
            if (rename.span.end.offset <= span.begin.offset) continue;
            if (rename.span.begin.offset >= span.end.offset) break;
            // Renames are name-only and cannot straddle a slice boundary.
            if (rename.span.begin.offset < span.begin.offset ||
                rename.span.end.offset > span.end.offset) {
                continue;
            }
            out += std::string(file.slice(SourceManager::makeSpan(
                &file, cursor, rename.span.begin.offset)));
            out += rename.text;
            cursor = rename.span.end.offset;
        }
        out += std::string(file.slice(SourceManager::makeSpan(&file, cursor, span.end.offset)));
        return out;
    }
};

void walkType(const TypeNode* type, Normalizer& norm);

void walkTypeParameters(const TypeParamList& params, Normalizer& norm) {
    for (const TypeParameterPtr& tp : params) {
        if (!tp) continue;
        walkType(tp->constraint.get(), norm);
        walkType(tp->defaultType.get(), norm);
    }
}

void walkSignatureMember(const SignatureMember* member, Normalizer& norm) {
    if (member == nullptr) return;
    switch (member->kind) {
        case NodeKind::PropertySignature: {
            const auto& prop = *static_cast<const PropertySignature*>(member);
            walkType(prop.type.get(), norm);
            return;
        }
        case NodeKind::MethodSignature: {
            const auto& method = *static_cast<const MethodSignature*>(member);
            walkTypeParameters(method.typeParams, norm);
            for (const ParameterPtr& param : method.params) {
                if (param) walkType(param->type.get(), norm);
            }
            walkType(method.returnType.get(), norm);
            return;
        }
        default:
            return;
    }
}

void walkType(const TypeNode* type, Normalizer& norm) {
    if (type == nullptr) return;
    switch (type->kind) {
        case NodeKind::NamedType: {
            const auto& named = *static_cast<const NamedType*>(type);
            norm.note(named);
            for (const TypePtr& arg : named.typeArgs) {
                walkType(arg.get(), norm);
            }
            return;
        }
        case NodeKind::UnionType:
            for (const TypePtr& part :
                 static_cast<const UnionType*>(type)->types) {
                walkType(part.get(), norm);
            }
            return;
        case NodeKind::ArrayType:
            walkType(static_cast<const ArrayType*>(type)->element.get(), norm);
            return;
        case NodeKind::TupleType:
            for (const TypePtr& part :
                 static_cast<const TupleType*>(type)->elements) {
                walkType(part.get(), norm);
            }
            return;
        case NodeKind::FunctionType: {
            const auto& fn = *static_cast<const FunctionType*>(type);
            walkTypeParameters(fn.typeParams, norm);
            for (const ParameterPtr& param : fn.params) {
                if (param) walkType(param->type.get(), norm);
            }
            walkType(fn.returnType.get(), norm);
            return;
        }
        case NodeKind::ObjectType:
            for (const SignatureMemberPtr& member :
                 static_cast<const ObjectType*>(type)->members) {
                walkSignatureMember(member.get(), norm);
            }
            return;
        case NodeKind::ParenthesizedType:
            walkType(static_cast<const ParenthesizedType*>(type)->inner.get(),
                     norm);
            return;
        default:
            return;  // PrefixedType is already neutral; LiteralType has no name.
    }
}

void walkParams(const ParamList& params, Normalizer& norm) {
    for (const ParameterPtr& param : params) {
        if (param) walkType(param->type.get(), norm);
    }
}

void collectFromFunctionLike(const TypeParamList& typeParams,
                             const ParamList& params,
                             const TypePtr& returnType, Normalizer& norm) {
    walkTypeParameters(typeParams, norm);
    walkParams(params, norm);
    walkType(returnType.get(), norm);
}

void collectFromClassMember(const ClassMember* member, Normalizer& norm) {
    if (member == nullptr) return;
    switch (member->kind) {
        case NodeKind::MethodDeclaration: {
            const auto& method = *static_cast<const MethodDeclaration*>(member);
            collectFromFunctionLike(method.typeParams, method.params,
                                    method.returnType, norm);
            return;
        }
        case NodeKind::ConstructorDeclaration: {
            const auto& ctor = *static_cast<const ConstructorDeclaration*>(member);
            walkParams(ctor.params, norm);
            return;
        }
        case NodeKind::PropertyDeclaration: {
            const auto& prop = *static_cast<const PropertyDeclaration*>(member);
            walkType(prop.type.get(), norm);
            return;
        }
        case NodeKind::AccessorDeclaration: {
            const auto& accessor = *static_cast<const AccessorDeclaration*>(member);
            walkParams(accessor.params, norm);
            walkType(accessor.returnType.get(), norm);
            return;
        }
        default:
            return;
    }
}

void collectFromStatement(const Stmt& stmt, Normalizer& norm);

void collectFromDeclaration(const Stmt& decl, Normalizer& norm) {
    switch (decl.kind) {
        case NodeKind::FunctionDeclaration: {
            const auto& fn = *static_cast<const FunctionDeclaration*>(&decl);
            collectFromFunctionLike(fn.typeParams, fn.params, fn.returnType,
                                    norm);
            return;
        }
        case NodeKind::ClassDeclaration: {
            const auto& cls = *static_cast<const ClassDeclaration*>(&decl);
            walkTypeParameters(cls.typeParams, norm);
            walkType(cls.extendsType.get(), norm);
            for (const TypePtr& impl : cls.implementsTypes) {
                walkType(impl.get(), norm);
            }
            for (const ClassMemberPtr& member : cls.members) {
                collectFromClassMember(member.get(), norm);
            }
            return;
        }
        case NodeKind::InterfaceDeclaration: {
            const auto& iface = *static_cast<const InterfaceDeclaration*>(&decl);
            walkTypeParameters(iface.typeParams, norm);
            for (const TypePtr& ext : iface.extendsTypes) {
                walkType(ext.get(), norm);
            }
            for (const SignatureMemberPtr& member : iface.members) {
                walkSignatureMember(member.get(), norm);
            }
            return;
        }
        case NodeKind::TypeAliasDeclaration: {
            const auto& alias = *static_cast<const TypeAliasDeclaration*>(&decl);
            walkTypeParameters(alias.typeParams, norm);
            walkType(alias.type.get(), norm);
            return;
        }
        case NodeKind::VariableDeclaration: {
            const auto& var = *static_cast<const VariableDeclaration*>(&decl);
            for (const VariableDeclaratorPtr& d : var.declarations) {
                if (d) walkType(d->type.get(), norm);
            }
            return;
        }
        default:
            return;
    }
}

void collectFromStatement(const Stmt& stmt, Normalizer& norm) {
    if (stmt.kind == NodeKind::ExportDeclaration) {
        const auto& exp = *static_cast<const ExportDeclaration*>(&stmt);
        if (exp.declaration) collectFromDeclaration(*exp.declaration, norm);
        return;
    }
    collectFromDeclaration(stmt, norm);
}

// Declaration text through the start of its body, closed with `;` - i.e. the
// signature. Declarations without a body are copied as written.
std::string withoutBody(const SourceFile& file, const Node& decl,
                        const Node* body, const Normalizer& norm) {
    if (body == nullptr || body->span.begin.offset <= decl.span.begin.offset) {
        return trimmed(norm.slice(decl.span)) + ";";
    }
    const SourceSpan head = SourceManager::makeSpan(
        &file, decl.span.begin.offset, body->span.begin.offset);
    return trimmed(norm.slice(head)) + ";";
}

// A class with every method/constructor/accessor body elided.
std::string stripClass(const SourceFile& file, const ClassDeclaration& cls,
                       const Normalizer& norm) {
    if (cls.members.empty()) return trimmed(norm.slice(cls.span));
    std::string out;
    const SourceSpan head = SourceManager::makeSpan(
        &file, cls.span.begin.offset, cls.members.front()->span.begin.offset);
    out += trimmed(norm.slice(head));
    for (const ClassMemberPtr& member : cls.members) {
        out += "\n  ";
        switch (member->kind) {
            case NodeKind::MethodDeclaration: {
                const auto& method =
                    static_cast<const MethodDeclaration&>(*member);
                out += withoutBody(file, method, method.body.get(), norm);
                break;
            }
            case NodeKind::ConstructorDeclaration: {
                const auto& ctor =
                    static_cast<const ConstructorDeclaration&>(*member);
                out += withoutBody(file, ctor, ctor.body.get(), norm);
                break;
            }
            case NodeKind::AccessorDeclaration: {
                const auto& accessor =
                    static_cast<const AccessorDeclaration&>(*member);
                out += withoutBody(file, accessor, accessor.body.get(), norm);
                break;
            }
            default:
                out += trimmed(norm.slice(member->span));
                break;
        }
    }
    out += "\n}";
    return out;
}

// Adds the `export` keyword unless the copied declaration text already
// carries it. The AST sometimes stores the keyword in the declaration span and
// sometimes only flags `isExported`; both forms end up with exactly one prefix.
std::string ensureExported(std::string text) {
    if (text.rfind("export ", 0) == 0 || text.rfind("export\t", 0) == 0) {
        return text;
    }
    return "export " + text;
}

std::string stripStatement(const SourceFile& file, const Stmt& stmt,
                           const Normalizer& norm) {
    switch (stmt.kind) {
        case NodeKind::FunctionDeclaration: {
            const auto& fn = static_cast<const FunctionDeclaration&>(stmt);
            const std::string text = withoutBody(file, fn, fn.body.get(), norm);
            return fn.isExported ? ensureExported(text) : text;
        }
        case NodeKind::ClassDeclaration: {
            const auto& cls = static_cast<const ClassDeclaration&>(stmt);
            const std::string text = stripClass(file, cls, norm);
            return cls.isExported ? ensureExported(text) : text;
        }
        case NodeKind::ExportDeclaration: {
            const auto& exp = static_cast<const ExportDeclaration&>(stmt);
            if (exp.declaration) {
                return ensureExported(stripStatement(file, *exp.declaration, norm));
            }
            // `export default { ... }`, `export { a, b }` and re-exports are
            // part of the surface exactly as written.
            return trimmed(norm.slice(stmt.span));
        }
        case NodeKind::VariableDeclaration:
        case NodeKind::ImportDeclaration:
        case NodeKind::InterfaceDeclaration:
        case NodeKind::EnumDeclaration:
        case NodeKind::TypeAliasDeclaration:
            return trimmed(norm.slice(stmt.span));
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

std::string stripToSignatures(const SourceFile& file, const Program& program,
                              TypeSystem typeSystem) {
    Normalizer norm(file, typeSystem);
    for (const StmtPtr& stmt : program.body) {
        if (stmt) collectFromStatement(*stmt, norm);
    }
    norm.sort();
    std::string out;
    out += "// index.cp3 - generated by cp3c from ";
    out += std::filesystem::path(file.path).filename().string();
    out += "; exported signatures and types only.\n";
    out += "// Do not edit: `cp3c build` regenerates this file for the .cp3pak.\n";
    for (const StmtPtr& stmt : program.body) {
        if (!stmt) continue;
        const std::string text = stripStatement(file, *stmt, norm);
        if (text.empty()) continue;
        out += "\n";
        out += text;
        out += "\n";
    }
    return out;
}

}  // namespace cp3::cli
