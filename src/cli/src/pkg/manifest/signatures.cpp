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
        for (const TypeNameRename& seen : renames) {
            if (seen.span.begin.offset == nameSpan.begin.offset &&
                seen.span.end.offset == nameSpan.end.offset) {
                return;  // already recorded (a node reached twice)
            }
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
                rename.span.end.offset > span.end.offset ||
                rename.span.begin.offset < cursor) {
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
void collectFromStatement(const Stmt& stmt, Normalizer& norm);

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

// Type spellings can also hide inside expressions that the surface copies
// verbatim: initializers, `export default <expr>`, explicit generic arguments
// (`f<i64>(...)`, `new Box<i64>()`), `as` casts, arrow-function parameter and
// return annotations, decorator arguments and computed keys. Renames are
// span-based, so collecting them here rewrites those copies too.
void collectFromExpr(const Expr* expr, Normalizer& norm);

void collectFromPattern(const Node* node, Normalizer& norm) {
    if (node == nullptr) return;
    switch (node->kind) {
        case NodeKind::ArrayPattern:
            for (const NodePtr& element :
                 static_cast<const ArrayPattern*>(node)->elements) {
                collectFromPattern(element.get(), norm);
            }
            collectFromPattern(static_cast<const ArrayPattern*>(node)
                                   ->rest.get(),
                               norm);
            return;
        case NodeKind::ObjectPattern: {
            const auto& pattern = *static_cast<const ObjectPattern*>(node);
            for (const ObjectPatternPropertyPtr& prop : pattern.properties) {
                if (!prop) continue;
                collectFromExpr(prop->key.get(), norm);
                collectFromPattern(prop->value.get(), norm);
            }
            collectFromPattern(pattern.rest.get(), norm);
            return;
        }
        case NodeKind::RestElement:
            collectFromPattern(
                static_cast<const RestElement*>(node)->argument.get(), norm);
            return;
        case NodeKind::AssignmentPattern: {
            const auto& pattern =
                *static_cast<const AssignmentPattern*>(node);
            collectFromPattern(pattern.left.get(), norm);
            collectFromExpr(pattern.right.get(), norm);
            return;
        }
        default:
            return;  // Identifier carries no type spelling.
    }
}

void collectFromExpr(const Expr* expr, Normalizer& norm) {
    if (expr == nullptr) return;
    switch (expr->kind) {
        case NodeKind::AsExpr: {
            const auto& cast = *static_cast<const AsExpr*>(expr);
            collectFromExpr(cast.expression.get(), norm);
            walkType(cast.type.get(), norm);
            return;
        }
        case NodeKind::CallExpr: {
            const auto& call = *static_cast<const CallExpr*>(expr);
            collectFromExpr(call.callee.get(), norm);
            for (const ExprPtr& arg : call.arguments) {
                collectFromExpr(arg.get(), norm);
            }
            for (const TypePtr& arg : call.typeArgs) {
                walkType(arg.get(), norm);
            }
            return;
        }
        case NodeKind::NewExpr: {
            const auto& created = *static_cast<const NewExpr*>(expr);
            collectFromExpr(created.callee.get(), norm);
            for (const ExprPtr& arg : created.arguments) {
                collectFromExpr(arg.get(), norm);
            }
            for (const TypePtr& arg : created.typeArgs) {
                walkType(arg.get(), norm);
            }
            return;
        }
        case NodeKind::ArrowFunction: {
            const auto& arrow = *static_cast<const ArrowFunction*>(expr);
            walkTypeParameters(arrow.typeParams, norm);
            for (const ParameterPtr& param : arrow.params) {
                if (!param) continue;
                collectFromPattern(param->name.get(), norm);
                walkType(param->type.get(), norm);
            }
            walkType(arrow.returnType.get(), norm);
            if (arrow.body) {
                if (arrow.body->kind == NodeKind::BlockStatement) {
                    collectFromStatement(
                        *static_cast<const Stmt*>(arrow.body.get()), norm);
                } else {
                    collectFromExpr(
                        static_cast<const Expr*>(arrow.body.get()), norm);
                }
            }
            return;
        }
        case NodeKind::ArrayLiteral:
            for (const ExprPtr& element :
                 static_cast<const ArrayLiteral*>(expr)->elements) {
                collectFromExpr(element.get(), norm);
            }
            return;
        case NodeKind::ObjectLiteral:
            for (const ObjectPropertyPtr& prop :
                 static_cast<const ObjectLiteral*>(expr)->properties) {
                if (!prop) continue;
                collectFromExpr(prop->key.get(), norm);
                collectFromExpr(prop->value.get(), norm);
            }
            return;
        case NodeKind::SpreadElement:
            collectFromExpr(
                static_cast<const SpreadElement*>(expr)->argument.get(),
                norm);
            return;
        case NodeKind::WeakExpr:
            collectFromExpr(
                static_cast<const WeakExpr*>(expr)->value.get(), norm);
            return;
        case NodeKind::MemberExpr:
            collectFromExpr(
                static_cast<const MemberExpr*>(expr)->object.get(), norm);
            return;
        case NodeKind::IndexExpr: {
            const auto& index = *static_cast<const IndexExpr*>(expr);
            collectFromExpr(index.object.get(), norm);
            collectFromExpr(index.index.get(), norm);
            return;
        }
        case NodeKind::UnaryExpr:
            collectFromExpr(
                static_cast<const UnaryExpr*>(expr)->operand.get(), norm);
            return;
        case NodeKind::UpdateExpr:
            collectFromExpr(
                static_cast<const UpdateExpr*>(expr)->operand.get(), norm);
            return;
        case NodeKind::BinaryExpr: {
            const auto& binary = *static_cast<const BinaryExpr*>(expr);
            collectFromExpr(binary.left.get(), norm);
            collectFromExpr(binary.right.get(), norm);
            return;
        }
        case NodeKind::LogicalExpr: {
            const auto& logical = *static_cast<const LogicalExpr*>(expr);
            collectFromExpr(logical.left.get(), norm);
            collectFromExpr(logical.right.get(), norm);
            return;
        }
        case NodeKind::ConditionalExpr: {
            const auto& conditional =
                *static_cast<const ConditionalExpr*>(expr);
            collectFromExpr(conditional.condition.get(), norm);
            collectFromExpr(conditional.whenTrue.get(), norm);
            collectFromExpr(conditional.whenFalse.get(), norm);
            return;
        }
        case NodeKind::AssignmentExpr: {
            const auto& assign = *static_cast<const AssignmentExpr*>(expr);
            collectFromExpr(assign.target.get(), norm);
            collectFromExpr(assign.value.get(), norm);
            return;
        }
        case NodeKind::AwaitExpr:
            collectFromExpr(
                static_cast<const AwaitExpr*>(expr)->argument.get(), norm);
            return;
        case NodeKind::RangeExpr: {
            const auto& range = *static_cast<const RangeExpr*>(expr);
            collectFromExpr(range.start.get(), norm);
            collectFromExpr(range.end.get(), norm);
            return;
        }
        case NodeKind::NonNullExpr:
            collectFromExpr(
                static_cast<const NonNullExpr*>(expr)->expression.get(),
                norm);
            return;
        case NodeKind::TemplateLiteral:
            for (const ExprPtr& element :
                 static_cast<const TemplateLiteral*>(expr)->expressions) {
                collectFromExpr(element.get(), norm);
            }
            return;
        default:
            return;  // Literals and identifiers carry no type spelling.
    }
}

void walkParams(const ParamList& params, Normalizer& norm) {
    for (const ParameterPtr& param : params) {
        if (!param) continue;
        collectFromPattern(param->name.get(), norm);
        walkType(param->type.get(), norm);
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
    // Decorator arguments appear in the copied declaration head.
    for (const ExprPtr& decorator : member->decorators) {
        collectFromExpr(decorator.get(), norm);
    }
    switch (member->kind) {
        case NodeKind::MethodDeclaration: {
            const auto& method = *static_cast<const MethodDeclaration*>(member);
            collectFromExpr(method.key.get(), norm);
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
            collectFromExpr(prop.key.get(), norm);
            walkType(prop.type.get(), norm);
            collectFromExpr(prop.initializer.get(), norm);
            return;
        }
        case NodeKind::AccessorDeclaration: {
            const auto& accessor = *static_cast<const AccessorDeclaration*>(member);
            collectFromExpr(accessor.key.get(), norm);
            walkParams(accessor.params, norm);
            walkType(accessor.returnType.get(), norm);
            return;
        }
        case NodeKind::StaticBlock:
            if (const StmtPtr& body =
                    static_cast<const StaticBlock*>(member)->body) {
                collectFromStatement(*body, norm);
            }
            return;
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
            collectFromExpr(cls.extendsExpr.get(), norm);
            for (const ExprPtr& decorator : cls.decorators) {
                collectFromExpr(decorator.get(), norm);
            }
            for (const TypePtr& impl : cls.implementsTypes) {
                walkType(impl.get(), norm);
            }
            for (const ClassMemberPtr& member : cls.members) {
                collectFromClassMember(member.get(), norm);
            }
            return;
        }
        case NodeKind::EnumDeclaration: {
            const auto& enumeration =
                *static_cast<const EnumDeclaration*>(&decl);
            for (const EnumMemberPtr& member : enumeration.members) {
                if (member) collectFromExpr(member->initializer.get(), norm);
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
                if (!d) continue;
                collectFromPattern(d->id.get(), norm);
                walkType(d->type.get(), norm);
                collectFromExpr(d->init.get(), norm);
            }
            return;
        }
        default:
            return;
    }
}

// `for (let i: i64 = ...)`-style slots (`init`/`left`) hold either a variable
// declaration or a bare expression/pattern.
void collectFromNodeSlot(const Node* node, Normalizer& norm) {
    if (node == nullptr) return;
    if (node->kind == NodeKind::VariableDeclaration ||
        node->kind == NodeKind::FunctionDeclaration ||
        node->kind == NodeKind::ClassDeclaration) {
        collectFromDeclaration(*static_cast<const Stmt*>(node), norm);
        return;
    }
    collectFromPattern(node, norm);
    collectFromExpr(static_cast<const Expr*>(node), norm);
}

// Annotation collection for statements whose text may be copied verbatim
// (initializers, `export default`, static blocks). Runtime-only top-level
// statements are dropped from the surface, so extra renames there are inert.
void collectFromStatement(const Stmt& stmt, Normalizer& norm) {
    switch (stmt.kind) {
        case NodeKind::ExportDeclaration: {
            const auto& exp = *static_cast<const ExportDeclaration*>(&stmt);
            if (exp.declaration) collectFromDeclaration(*exp.declaration, norm);
            collectFromExpr(exp.defaultExpr.get(), norm);
            return;
        }
        case NodeKind::expressionstatement:
            collectFromExpr(
                static_cast<const expressionstatement*>(&stmt)
                    ->expression.get(),
                norm);
            return;
        case NodeKind::BlockStatement:
            for (const StmtPtr& inner :
                 static_cast<const BlockStatement*>(&stmt)->body) {
                if (inner) collectFromStatement(*inner, norm);
            }
            return;
        case NodeKind::IfStatement: {
            const auto& branch = *static_cast<const IfStatement*>(&stmt);
            collectFromExpr(branch.condition.get(), norm);
            if (branch.thenBranch) collectFromStatement(*branch.thenBranch, norm);
            if (branch.elseBranch) collectFromStatement(*branch.elseBranch, norm);
            return;
        }
        case NodeKind::WhileStatement: {
            const auto& loop = *static_cast<const WhileStatement*>(&stmt);
            collectFromExpr(loop.condition.get(), norm);
            if (loop.body) collectFromStatement(*loop.body, norm);
            return;
        }
        case NodeKind::DoWhileStatement: {
            const auto& loop = *static_cast<const DoWhileStatement*>(&stmt);
            if (loop.body) collectFromStatement(*loop.body, norm);
            collectFromExpr(loop.condition.get(), norm);
            return;
        }
        case NodeKind::ForStatement: {
            const auto& loop = *static_cast<const ForStatement*>(&stmt);
            collectFromNodeSlot(loop.init.get(), norm);
            collectFromExpr(loop.condition.get(), norm);
            collectFromExpr(loop.update.get(), norm);
            if (loop.body) collectFromStatement(*loop.body, norm);
            return;
        }
        case NodeKind::ForOfStatement: {
            const auto& loop = *static_cast<const ForOfStatement*>(&stmt);
            collectFromNodeSlot(loop.left.get(), norm);
            collectFromExpr(loop.right.get(), norm);
            if (loop.body) collectFromStatement(*loop.body, norm);
            return;
        }
        case NodeKind::ForInStatement: {
            const auto& loop = *static_cast<const ForInStatement*>(&stmt);
            collectFromNodeSlot(loop.left.get(), norm);
            collectFromExpr(loop.right.get(), norm);
            if (loop.body) collectFromStatement(*loop.body, norm);
            return;
        }
        case NodeKind::SwitchStatement: {
            const auto& selection =
                *static_cast<const SwitchStatement*>(&stmt);
            collectFromExpr(selection.discriminant.get(), norm);
            for (const SwitchCasePtr& branch : selection.cases) {
                if (!branch) continue;
                collectFromExpr(branch->test.get(), norm);
                for (const StmtPtr& inner : branch->statements) {
                    if (inner) collectFromStatement(*inner, norm);
                }
            }
            return;
        }
        case NodeKind::ReturnStatement:
            collectFromExpr(
                static_cast<const ReturnStatement*>(&stmt)->argument.get(),
                norm);
            return;
        case NodeKind::ThrowStatement:
            collectFromExpr(
                static_cast<const ThrowStatement*>(&stmt)->argument.get(),
                norm);
            return;
        case NodeKind::TryStatement: {
            const auto& guard = *static_cast<const TryStatement*>(&stmt);
            if (guard.tryBlock) collectFromStatement(*guard.tryBlock, norm);
            if (guard.handler) {
                collectFromPattern(guard.handler->param.get(), norm);
                if (guard.handler->body) {
                    collectFromStatement(*guard.handler->body, norm);
                }
            }
            if (guard.finallyBlock) {
                collectFromStatement(*guard.finallyBlock, norm);
            }
            return;
        }
        case NodeKind::LabeledStatement:
            if (const StmtPtr& body =
                    static_cast<const LabeledStatement*>(&stmt)->body) {
                collectFromStatement(*body, norm);
            }
            return;
        default:
            collectFromDeclaration(stmt, norm);
            return;
    }
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
            case NodeKind::StaticBlock: {
                // Runtime initialization, not surface: keep the marker, drop
                // the body (its text is copied verbatim by `default` below,
                // which would also leak un-rewritten annotations).
                const auto& block =
                    static_cast<const StaticBlock&>(*member);
                if (block.body &&
                    block.body->span.begin.offset > member->span.begin.offset) {
                    const SourceSpan head = SourceManager::makeSpan(
                        &file, member->span.begin.offset,
                        block.body->span.begin.offset);
                    out += trimmed(norm.slice(head)) + "}";
                } else {
                    out += trimmed(norm.slice(member->span));
                }
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
                           const Normalizer& norm,
                           const std::vector<std::string>& forceExports);

std::string stripStatement(const SourceFile& file, const Stmt& stmt,
                           const Normalizer& norm) {
    return stripStatement(file, stmt, norm, {});
}

bool isForceExported(const std::vector<std::string>& forceExports,
                     const std::string& name) {
    return std::find(forceExports.begin(), forceExports.end(), name) !=
           forceExports.end();
}

std::string stripStatement(const SourceFile& file, const Stmt& stmt,
                           const Normalizer& norm,
                           const std::vector<std::string>& forceExports) {
    switch (stmt.kind) {
        case NodeKind::FunctionDeclaration: {
            const auto& fn = static_cast<const FunctionDeclaration&>(stmt);
            const std::string text = withoutBody(file, fn, fn.body.get(), norm);
            return fn.isExported ? ensureExported(text) : text;
        }
        case NodeKind::ClassDeclaration: {
            const auto& cls = static_cast<const ClassDeclaration&>(stmt);
            const std::string text = stripClass(file, cls, norm);
            // A class reached through the default export (`export default
            // { Counter }` / `export default Counter`) or an export list
            // (`export { Counter }`) is surfaced as a real named export here,
            // so a consumer can `import { Counter } from "<pkg>"` and
            // `new` it. The default-object member path
            // (`new pkg.Counter()`) keeps the generated default export
            // verbatim; this named surface is what type-checks against the
            // class type without the CP3401 default-object class-value
            // rejection. `ensureExported` is idempotent, so an already
            // exported class is not doubled.
            if (cls.isExported || isForceExported(forceExports, cls.name)) {
                return ensureExported(text);
            }
            return text;
        }
        case NodeKind::ExportDeclaration: {
            const auto& exp = static_cast<const ExportDeclaration&>(stmt);
            if (exp.declaration) {
                return ensureExported(
                    stripStatement(file, *exp.declaration, norm, forceExports));
            }
            // `export default { ... }` stays verbatim (see the class note
            // above: its classes are also available as named exports).
            if (exp.kind == ExportKind::Default) {
                return trimmed(norm.slice(stmt.span));
            }
            if (exp.kind == ExportKind::Named && exp.source.empty()) {
                // Specifiers that are force-exported classes are already
                // surfaced as `export class <Name>` on their own
                // declaration; re-exporting the same name would be a
                // duplicate-export error, so they are dropped here.
                // Re-exports with a source (`export { x } from "m"`) keep
                // their `from` clause verbatim below.
                std::vector<std::string> parts;
                for (const ExportSpecifierPtr& spec : exp.specifiers) {
                    if (!spec) continue;
                    if (isForceExported(forceExports, spec->local)) continue;
                    std::string part = spec->local;
                    if (!spec->exported.empty() &&
                        spec->exported != spec->local) {
                        part += " as " + spec->exported;
                    }
                    parts.push_back(part);
                }
                if (parts.empty()) return {};
                std::string text = "export { ";
                for (std::size_t i = 0; i < parts.size(); ++i) {
                    if (i != 0) text += ", ";
                    text += parts[i];
                }
                text += " };";
                return text;
            }
            // `export default { ... }`, re-exports with a source and
            // `export * ...` are part of the surface exactly as written.
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

// Names of top-level classes the package surfaces through its default export
// (`export default Counter`, `export default { Counter }`) or an export list
// (`export { Counter }`). These names are force-exported by the stripper.
// `export default class Counter {}` is parsed as a Default export carrying
// the class declaration.
void collectExportedClassNames(const Expr* expr,
                               std::vector<std::string>& out) {
    if (expr == nullptr) return;
    if (expr->kind == NodeKind::Identifier) {
        const std::string& name = static_cast<const Identifier*>(expr)->name;
        if (!name.empty() &&
            std::find(out.begin(), out.end(), name) == out.end()) {
            out.push_back(name);
        }
        return;
    }
    if (expr->kind == NodeKind::ObjectLiteral) {
        for (const ObjectPropertyPtr& prop :
             static_cast<const ObjectLiteral*>(expr)->properties) {
            if (!prop) continue;
            // Shorthand (`{ a }`) and value form (`{ a: a }`) both carry the
            // class name as the value identifier.
            const Expr* value = prop->value.get();
            if (value == nullptr && prop->key) value = prop->key.get();
            collectExportedClassNames(value, out);
        }
    }
}

std::vector<std::string> forceExportClassNames(const Program& program) {
    // Only names that are real top-level classes can be force-exported: a
    // function value reached through the same default object (`export
    // default { add }`) or an export list (`export { add }`) keeps its
    // original form and must not be dropped by the Named filter.
    std::vector<std::string> classNames;
    for (const StmtPtr& stmt : program.body) {
        if (!stmt) continue;
        const Stmt* decl = stmt.get();
        if (decl->kind == NodeKind::ExportDeclaration) {
            const auto& exp = static_cast<const ExportDeclaration*>(decl)->declaration;
            if (exp && exp->kind == NodeKind::ClassDeclaration) {
                const std::string& name =
                    static_cast<const ClassDeclaration&>(*exp).name;
                if (!name.empty() &&
                    std::find(classNames.begin(), classNames.end(), name) ==
                        classNames.end()) {
                    classNames.push_back(name);
                }
            }
            continue;
        }
        if (decl->kind == NodeKind::ClassDeclaration) {
            const std::string& name =
                static_cast<const ClassDeclaration*>(decl)->name;
            if (!name.empty() &&
                std::find(classNames.begin(), classNames.end(), name) ==
                    classNames.end()) {
                classNames.push_back(name);
            }
        }
    }
    std::vector<std::string> referenced;
    for (const StmtPtr& stmt : program.body) {
        if (!stmt || stmt->kind != NodeKind::ExportDeclaration) continue;
        const auto& exp = static_cast<const ExportDeclaration&>(*stmt);
        if (exp.kind == ExportKind::Default && exp.defaultExpr) {
            collectExportedClassNames(exp.defaultExpr.get(), referenced);
        }
        if (exp.kind == ExportKind::Named && exp.source.empty()) {
            for (const ExportSpecifierPtr& spec : exp.specifiers) {
                if (!spec || spec->local.empty()) continue;
                if (std::find(referenced.begin(), referenced.end(), spec->local) ==
                    referenced.end()) {
                    referenced.push_back(spec->local);
                }
            }
        }
    }
    std::vector<std::string> out;
    for (const std::string& name : referenced) {
        if (std::find(classNames.begin(), classNames.end(), name) !=
            classNames.end()) {
            out.push_back(name);
        }
    }
    return out;
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

bool hasPackageExportSurface(const Program& program) {
    if (hasDefaultExport(program)) return true;
    for (const StmtPtr& stmt : program.body) {
        if (!stmt || stmt->kind != NodeKind::ExportDeclaration) continue;
        const auto& exp = static_cast<const ExportDeclaration&>(*stmt);
        if (exp.declaration != nullptr) return true;
        if (exp.kind == ExportKind::Named && !exp.specifiers.empty()) {
            return true;
        }
        if (exp.kind == ExportKind::ReExportNamed ||
            exp.kind == ExportKind::ReExportAll) {
            return true;
        }
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
    const std::vector<std::string> forceExports = forceExportClassNames(program);
    std::string out;
    out += "// index.cp3 - generated by cp3c from ";
    out += std::filesystem::path(file.path).filename().string();
    out += "; exported signatures and types only.\n";
    out += "// Do not edit: `cp3c build` regenerates this file for the .cp3pak.\n";
    for (const StmtPtr& stmt : program.body) {
        if (!stmt) continue;
        const std::string text = stripStatement(file, *stmt, norm, forceExports);
        if (text.empty()) continue;
        out += "\n";
        out += text;
        out += "\n";
    }
    return out;
}

}  // namespace cp3::cli
