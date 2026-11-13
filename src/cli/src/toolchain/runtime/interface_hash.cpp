// =============================================================================
// src/cli/src/toolchain/runtime/interface_hash.cpp - canonical interface hashes.
//
// OWNERSHIP: cli module (src/cli/**). Implements detail/toolchain/interface_hash.hpp.
//
// Pure in-memory text builders: no I/O, no exceptions across the module
// boundary (interfaceHashOf is noexcept and swallows allocation failure as 0).
// Every iteration happens over vectors in declaration/source order or over a
// canonically re-sorted copy; no hash-table iteration order reaches the
// output. Union member order in type texts is preserved as stored.
// =============================================================================
#include "detail/toolchain/interface_hash.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

#include "cp3/core/ast.hpp"
#include "cp3/core/types.hpp"
#include "cp3/sema.hpp"

#include "detail/core/util.hpp"  // fnv1a64

namespace cp3::cli {
namespace {

// -----------------------------------------------------------------------------
// Low-level writers: every payload is either a fixed-vocabulary token or a
// length-prefixed `<byte-len>:<raw bytes>` string.
// -----------------------------------------------------------------------------

void putStr(std::string& out, std::string_view s) {
    out += std::to_string(s.size());
    out += ':';
    out += s;
}

// A comma-separated list of length-prefixed strings inside parentheses.
void putStrList(std::string& out, const std::vector<std::string>& items) {
    out += std::to_string(items.size());
    out += '(';
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i != 0) out += ',';
        putStr(out, items[i]);
    }
    out += ')';
}

// A comma-separated list of bare 0/1 tokens inside parentheses.
void putBoolList(std::string& out, const std::vector<std::string>& items) {
    out += std::to_string(items.size());
    out += '(';
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i != 0) out += ',';
        out += (items[i] == "1" ? '1' : '0');
    }
    out += ')';
}

// Escapes '\\', control bytes and DEL as \\xNN (\\n, \\r, \\t for those);
// everything else (including UTF-8 bytes >= 0x80) is emitted verbatim.
std::string escapePayload(std::string_view s) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(s.size() + 8);
    for (const unsigned char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20 || c == 0x7f) {
                    out += "\\x";
                    out += kHex[(c >> 4) & 0xf];
                    out += kHex[c & 0xf];
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

// Shortest round-trip spelling of a double (std::to_chars general format).
std::string shortestRoundTrip(double value) {
    char buf[32];
    const auto res = std::to_chars(buf, buf + sizeof buf, value,
                                   std::chars_format::general);
    if (res.ec == std::errc{}) return std::string(buf, res.ptr);
    return std::to_string(value);
}

std::string joinWith(const std::vector<std::string>& parts, std::string_view sep) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i != 0) out += sep;
        out += parts[i];
    }
    return out;
}

std::string_view accessSuffix(AccessModifier access) {
    switch (access) {
        case AccessModifier::Public: return "public";
        case AccessModifier::Private: return "private";
        case AccessModifier::Protected: return "protected";
        case AccessModifier::None: break;
    }
    return {};
}

void addSuffix(std::string& out, std::string_view word) {
    if (word.empty()) return;
    out += ' ';
    out += word;
}

void addSuffix(std::string& out, bool present, std::string_view word) {
    if (present) addSuffix(out, word);
}

void addAccess(std::string& out, AccessModifier access) {
    addSuffix(out, accessSuffix(access));
}

// -----------------------------------------------------------------------------
// TypeNode -> canonical text (alias bodies, heritage types, member annotations).
// Declared before use by the type-parameter printer below.
// -----------------------------------------------------------------------------
std::string typeNodeToText(const TypeNode& node);

std::string typeParamsSuffix(const TypeParamList& params) {
    if (params.empty()) return {};
    std::string out = "<";
    for (std::size_t i = 0; i < params.size(); ++i) {
        if (i != 0) out += ", ";
        const TypeParameterPtr& p = params[i];
        if (!p) {
            out += '?';
            continue;
        }
        out += escapePayload(p->name);
        if (p->constraint) out += " extends " + typeNodeToText(*p->constraint);
        if (p->defaultType) out += " = " + typeNodeToText(*p->defaultType);
    }
    out += '>';
    return out;
}

// Shallow print of the name-like expression used in heritage/key positions.
std::string shallowExprText(const Expr& expr) {
    if (expr.kind == NodeKind::Identifier) {
        return escapePayload(static_cast<const Identifier&>(expr).name);
    }
    if (expr.kind == NodeKind::MemberExpr) {
        const auto& member = static_cast<const MemberExpr&>(expr);
        if (member.object) {
            return shallowExprText(*member.object) + "." +
                   escapePayload(member.property);
        }
    }
    return std::string("<") + nodeKindName(expr.kind) + ">";
}

// The canonical text of a member's key expression.
std::string keyToText(const Expr* key, bool computed) {
    if (key == nullptr) return "<no-key>";
    if (computed) {
        return std::string("[") + nodeKindName(key->kind) + "]";
    }
    switch (key->kind) {
        case NodeKind::Identifier:
            return escapePayload(static_cast<const Identifier&>(*key).name);
        case NodeKind::StringLiteral:
            return "\"" +
                   escapePayload(static_cast<const StringLiteral&>(*key).value) +
                   "\"";
        case NodeKind::NumberLiteral: {
            const auto& literal = static_cast<const NumberLiteral&>(*key);
            return escapePayload(literal.raw.empty()
                                     ? shortestRoundTrip(literal.value)
                                     : literal.raw);
        }
        case NodeKind::BoolLiteral:
            return static_cast<const BoolLiteral&>(*key).value ? "true" : "false";
        default:
            return std::string("<") + nodeKindName(key->kind) + ">";
    }
}

// The declared parameter type (or `unknown`), with rest/ref/optional markers.
std::string paramText(const Parameter& p) {
    std::string out;
    if (p.isRest) out += "...";
    if (p.isRef) out += "ref ";
    out += p.type ? typeNodeToText(*p.type) : std::string("unknown");
    if (p.optional) out += '?';
    return out;
}

std::string paramsText(const ParamList& params) {
    std::vector<std::string> parts;
    parts.reserve(params.size());
    for (const ParameterPtr& p : params) {
        parts.push_back(p ? paramText(*p) : std::string("unknown"));
    }
    return joinWith(parts, ", ");
}

std::string typePtrText(const TypeNode* t) {
    return t ? typeNodeToText(*t) : std::string("unknown");
}

void addGetSetSuffix(std::string& out, AccessorKind kind) {
    addSuffix(out, kind == AccessorKind::Set ? "set" : "get");
}

// -----------------------------------------------------------------------------
// Member line builders. All lines are single-line, '\n'-free texts.
// -----------------------------------------------------------------------------

std::string classMemberLine(const ClassMember& member) {
    switch (member.kind) {
        case NodeKind::PropertyDeclaration: {
            const auto& p = static_cast<const PropertyDeclaration&>(member);
            std::string line = "field ";
            line += keyToText(p.key.get(), p.computed);
            if (p.isDefiniteAssignment) line += '!';
            if (p.optional) line += '?';
            line += ": ";
            line += typePtrText(p.type.get());
            addAccess(line, p.access);
            addSuffix(line, p.isStatic, "static");
            addSuffix(line, p.isAbstract, "abstract");
            addSuffix(line, p.isReadonly, "readonly");
            addSuffix(line, p.isOverride, "override");
            addSuffix(line, p.isWeak, "weak");
            return line;
        }
        case NodeKind::MethodDeclaration: {
            const auto& m = static_cast<const MethodDeclaration&>(member);
            std::string line = "method ";
            line += keyToText(m.key.get(), m.computed);
            if (m.optional) line += '?';
            line += '(';
            line += paramsText(m.params);
            line += ") -> ";
            line += typePtrText(m.returnType.get());
            addAccess(line, m.access);
            addSuffix(line, m.isStatic, "static");
            addSuffix(line, m.isAbstract, "abstract");
            addSuffix(line, m.isAsync, "async");
            addSuffix(line, m.isReadonly, "readonly");
            addSuffix(line, m.isOverride, "override");
            return line;
        }
        case NodeKind::ConstructorDeclaration: {
            const auto& c = static_cast<const ConstructorDeclaration&>(member);
            std::string line = "constructor(";
            line += paramsText(c.params);
            line += ')';
            addAccess(line, c.access);
            return line;
        }
        case NodeKind::StaticBlock:
            return "static-block";
        case NodeKind::AccessorDeclaration: {
            const auto& a = static_cast<const AccessorDeclaration&>(member);
            std::string line = "accessor ";
            line += keyToText(a.key.get(), a.computed);
            line += '(';
            line += paramsText(a.params);
            line += ") -> ";
            line += typePtrText(a.returnType.get());
            addAccess(line, a.access);
            addSuffix(line, a.isStatic, "static");
            addGetSetSuffix(line, a.accessor);
            return line;
        }
        default:
            return std::string("unknown ") + nodeKindName(member.kind);
    }
}

std::string signatureMemberLine(const SignatureMember& member) {
    switch (member.kind) {
        case NodeKind::PropertySignature: {
            const auto& p = static_cast<const PropertySignature&>(member);
            std::string line = "prop ";
            line += keyToText(p.key.get(), p.computed);
            if (p.optional) line += '?';
            line += ": ";
            line += typePtrText(p.type.get());
            addSuffix(line, p.isReadonly, "readonly");
            return line;
        }
        case NodeKind::MethodSignature: {
            const auto& m = static_cast<const MethodSignature&>(member);
            std::string line = "method ";
            line += keyToText(m.key.get(), m.computed);
            if (m.optional) line += '?';
            line += '(';
            line += paramsText(m.params);
            line += ") -> ";
            line += typePtrText(m.returnType.get());
            addSuffix(line, m.hasFunctionKeyword, "function");
            return line;
        }
        case NodeKind::IndexSignature: {
            const auto& i = static_cast<const IndexSignature&>(member);
            std::string line = "index (";
            line += typePtrText(i.keyType.get());
            line += ") -> ";
            line += typePtrText(i.valueType.get());
            addSuffix(line, i.isReadonly, "readonly");
            return line;
        }
        default:
            return std::string("unknown ") + nodeKindName(member.kind);
    }
}

std::string shapeText(const std::string& label, const std::vector<std::string>& lines) {
    std::string out = label;
    for (const std::string& line : lines) {
        out += '\n';
        out += line;
    }
    return out;
}

std::string typeNodeToText(const TypeNode& node) {
    switch (node.kind) {
        case NodeKind::NamedType: {
            const auto& n = static_cast<const NamedType&>(node);
            std::string out = escapePayload(n.name);
            if (!n.typeArgs.empty()) {
                std::vector<std::string> args;
                args.reserve(n.typeArgs.size());
                for (const TypePtr& arg : n.typeArgs) {
                    args.push_back(arg ? typeNodeToText(*arg) : std::string("unknown"));
                }
                out += '<';
                out += joinWith(args, ", ");
                out += '>';
            }
            return out;
        }
        case NodeKind::UnionType: {
            const auto& u = static_cast<const UnionType&>(node);
            std::vector<std::string> members;
            members.reserve(u.types.size());
            for (const TypePtr& part : u.types) {
                if (!part) {
                    members.push_back("unknown");
                    continue;
                }
                std::string text = typeNodeToText(*part);
                if (part->kind == NodeKind::UnionType || part->kind == NodeKind::FunctionType) {
                    text = '(' + text + ')';
                }
                members.push_back(std::move(text));
            }
            return joinWith(members, " | ");
        }
        case NodeKind::ArrayType: {
            const auto& a = static_cast<const ArrayType&>(node);
            return '(' + typePtrText(a.element.get()) + ")[]";
        }
        case NodeKind::TupleType: {
            const auto& t = static_cast<const TupleType&>(node);
            std::vector<std::string> elements;
            elements.reserve(t.elements.size());
            for (const TypePtr& element : t.elements) {
                elements.push_back(element ? typeNodeToText(*element) : std::string("unknown"));
            }
            return '[' + joinWith(elements, ", ") + ']';
        }
        case NodeKind::FunctionType: {
            const auto& f = static_cast<const FunctionType&>(node);
            std::string out = typeParamsSuffix(f.typeParams);
            out += '(';
            out += paramsText(f.params);
            out += ") => ";
            out += typePtrText(f.returnType.get());
            return out;
        }
        case NodeKind::ObjectType: {
            const auto& o = static_cast<const ObjectType&>(node);
            std::vector<std::string> members;
            members.reserve(o.members.size());
            for (const SignatureMemberPtr& member : o.members) {
                members.push_back(member ? signatureMemberLine(*member)
                                         : std::string("unknown <null>"));
            }
            return '{' + joinWith(members, "; ") + '}';
        }
        case NodeKind::PrefixedType: {
            const auto& p = static_cast<const PrefixedType&>(node);
            return escapePayload(p.prefix) + "'" + escapePayload(p.name) + "'";
        }
        case NodeKind::LiteralType: {
            const auto& l = static_cast<const LiteralType&>(node);
            switch (l.literalKind) {
                case LiteralTypeKind::String:
                    return "literal:string:\"" + escapePayload(l.stringValue) + "\"";
                case LiteralTypeKind::Number:
                    return "literal:number:" +
                           escapePayload(l.numberRaw.empty()
                                             ? shortestRoundTrip(l.numberValue)
                                             : l.numberRaw);
                case LiteralTypeKind::Bool:
                    return std::string("literal:boolean:") + (l.boolValue ? "true" : "false");
            }
            return "literal:unknown-base:unknown";
        }
        case NodeKind::ParenthesizedType: {
            const auto& p = static_cast<const ParenthesizedType&>(node);
            return '(' + typePtrText(p.inner.get()) + ')';
        }
        default:
            return std::string("unknown-kind:") + nodeKindName(node.kind);
    }
}

bool isUnaryAmbiguousForUnionPart(TypeKind kind) {
    return kind == TypeKind::Union || kind == TypeKind::Function ||
           kind == TypeKind::Nullable;
}

void addPartText(const Type& t, std::string& out) {
    std::string inner = typeToCanonicalText(t);
    if (isUnaryAmbiguousForUnionPart(t.kind)) {
        out += '(';
        out += inner;
        out += ')';
    } else {
        out += inner;
    }
}

// The record for one export entry (no trailing '\n').
std::string entryLine(const InterfaceExportEntry& entry) {
    std::string out = "entry kind=";
    putStr(out, entry.kind);
    out += " name=";
    putStr(out, entry.name);
    out += " field=";
    putStr(out, entry.binding.name);
    out += " type=";
    putStr(out, entry.binding.typeText);
    out += " flags=";
    out += entry.binding.isValue ? '1' : '0';
    out += entry.binding.isType ? '1' : '0';
    out += entry.binding.isFunction ? '1' : '0';
    out += entry.binding.isAsync ? '1' : '0';
    out += entry.binding.hasRest ? '1' : '0';
    out += " typeParams=";
    putStrList(out, entry.binding.typeParams);
    out += " paramTypes=";
    putStrList(out, entry.binding.paramTypes);
    out += " paramOptional=";
    putBoolList(out, entry.binding.paramOptional);
    out += " returnType=";
    putStr(out, entry.binding.returnType);
    return out;
}

// One `class` record for a shape (no trailing '\n').
std::string classRecord(const InterfaceClassShape& shape) {
    std::string out = "class label=";
    putStr(out, shape.label);
    out += " members=";
    out += std::to_string(shape.memberText.size());
    out += '{';
    for (std::size_t i = 0; i < shape.memberText.size(); ++i) {
        if (i != 0) out += ';';
        putStr(out, shape.memberText[i]);
    }
    out += '}';
    return out;
}

}  // namespace

std::string typeToCanonicalText(const Type& t) {
    switch (t.kind) {
        case TypeKind::Error: return "error";
        case TypeKind::Unknown: return "unknown";
        case TypeKind::Number: return "number";
        case TypeKind::String: return "string";
        case TypeKind::Boolean: return "boolean";
        case TypeKind::Undefined: return "undefined";
        case TypeKind::Null: return "null";
        case TypeKind::Void: return "void";
        case TypeKind::Array: {
            const Type* element = t.parts.empty() ? nullptr : &t.parts.front();
            return '(' + (element ? typeToCanonicalText(*element) : std::string("unknown")) +
                   ")[]";
        }
        case TypeKind::Function: {
            std::string out = "(";
            if (t.parts.empty()) {
                out += ") => unknown";
                return out;
            }
            for (std::size_t i = 0; i + 1 < t.parts.size(); ++i) {
                if (i != 0) out += ", ";
                out += typeToCanonicalText(t.parts[i]);
            }
            out += ") => ";
            out += typeToCanonicalText(t.parts.back());
            return out;
        }
        case TypeKind::Object:
            return t.name.empty() ? std::string("object")
                                  : "object:" + escapePayload(t.name);
        case TypeKind::Class:
            return t.name.empty() ? std::string("class")
                                  : "class:" + escapePayload(t.name);
        case TypeKind::Enum:
            return t.name.empty() ? std::string("enum")
                                  : "enum:" + escapePayload(t.name);
        case TypeKind::Union: {
            if (t.parts.empty()) return "union";
            std::string out;
            for (std::size_t i = 0; i < t.parts.size(); ++i) {
                if (i != 0) out += " | ";
                addPartText(t.parts[i], out);
            }
            return out;
        }
        case TypeKind::Nullable: {
            const Type* inner = t.parts.empty() ? nullptr : &t.parts.front();
            return '(' + (inner ? typeToCanonicalText(*inner) : std::string("unknown")) + ')' +
                   '?';
        }
        case TypeKind::TypeParam:
            return t.name.empty() ? std::string("typeparam")
                                  : "typeparam:" + escapePayload(t.name);
        case TypeKind::Literal: {
            switch (t.literalBase) {
                case TypeKind::Number:
                    return "literal:number:" +
                           escapePayload(t.numberRaw.empty()
                                             ? shortestRoundTrip(t.numberValue)
                                             : t.numberRaw);
                case TypeKind::String:
                    return "literal:string:\"" + escapePayload(t.stringValue) + "\"";
                case TypeKind::Boolean:
                    return std::string("literal:boolean:") +
                           (t.boolValue ? "true" : "false");
                default:
                    return std::string("literal:") + typeKindName(t.literalBase) +
                           ":unknown";
            }
        }
        case TypeKind::Native:
            return t.name.empty() ? std::string("native")
                                  : "native:" + escapePayload(t.name);
    }
    return std::string("unknown-kind:") + typeKindName(t.kind);
}

InterfaceExportEntry importedBindingToEntry(const std::string& name,
                                            const ImportedBinding& b,
                                            const std::string& kind) {
    InterfaceExportEntry entry;
    entry.name = name;
    entry.kind = kind;
    InterfaceBindingField& field = entry.binding;
    field.name = name;
    field.typeText = typeToCanonicalText(b.type);
    field.isValue = b.isValue;
    field.isType = b.isType;
    field.isFunction = b.isFunction;
    field.isAsync = b.isAsync;
    field.hasRest = b.hasRest;
    field.typeParams.reserve(b.typeParams.size());
    for (const Type& t : b.typeParams) field.typeParams.push_back(typeToCanonicalText(t));
    field.paramTypes.reserve(b.paramTypes.size());
    for (const Type& t : b.paramTypes) field.paramTypes.push_back(typeToCanonicalText(t));
    field.paramOptional.reserve(b.paramOptional.size());
    for (const bool optional : b.paramOptional)
        field.paramOptional.push_back(optional ? "1" : "0");
    field.returnType = typeToCanonicalText(b.returnType);
    return entry;
}

std::string bindingToInterfaceEntry(const std::string& name, const ImportedBinding& b,
                                    const std::string& kind) {
    return entryLine(importedBindingToEntry(name, b, kind));
}

std::string classDeclToInterfaceShape(const ClassDeclaration& decl) {
    std::string label = decl.isAbstract ? "abstract class " : "class ";
    label += escapePayload(decl.name);
    label += typeParamsSuffix(decl.typeParams);
    std::vector<std::string> lines;
    if (decl.extendsType) {
        lines.push_back("extends " + typeNodeToText(*decl.extendsType));
    } else if (decl.extendsExpr) {
        lines.push_back("extends " + shallowExprText(*decl.extendsExpr));
    }
    for (const TypePtr& impl : decl.implementsTypes) {
        lines.push_back(impl ? "implements " + typeNodeToText(*impl)
                             : std::string("implements unknown"));
    }
    for (const ClassMemberPtr& member : decl.members) {
        if (!member) {
            lines.push_back("unknown <null>");
            continue;
        }
        lines.push_back(classMemberLine(*member));
    }
    return shapeText(label, lines);
}

std::string interfaceDeclToInterfaceShape(const InterfaceDeclaration& decl) {
    std::string label = "interface ";
    label += escapePayload(decl.name);
    label += typeParamsSuffix(decl.typeParams);
    std::vector<std::string> lines;
    for (const TypePtr& base : decl.extendsTypes) {
        lines.push_back(base ? "extends " + typeNodeToText(*base)
                             : std::string("extends unknown"));
    }
    for (const SignatureMemberPtr& member : decl.members) {
        if (!member) {
            lines.push_back("unknown <null>");
            continue;
        }
        lines.push_back(signatureMemberLine(*member));
    }
    return shapeText(label, lines);
}

std::string aliasDeclToInterfaceShape(const TypeAliasDeclaration& decl) {
    std::string label = "type ";
    label += escapePayload(decl.name);
    label += typeParamsSuffix(decl.typeParams);
    std::vector<std::string> lines;
    std::string line = "alias ";
    line += escapePayload(decl.name);
    line += " = ";
    line += typePtrText(decl.type.get());
    lines.push_back(std::move(line));
    return shapeText(label, lines);
}

std::string serializeInterfaceSignature(const std::vector<InterfaceExportEntry>& entries,
                                        const std::vector<InterfaceClassShape>& classes) {
    struct RenderedEntry {
        std::string name;
        std::string kind;
        std::string line;
    };
    std::vector<RenderedEntry> renderedEntries;
    renderedEntries.reserve(entries.size());
    for (const InterfaceExportEntry& entry : entries) {
        renderedEntries.push_back({entry.name, entry.kind, entryLine(entry)});
    }
    std::stable_sort(renderedEntries.begin(), renderedEntries.end(),
                     [](const RenderedEntry& a, const RenderedEntry& b) {
                         return std::tie(a.name, a.kind, a.line) <
                                std::tie(b.name, b.kind, b.line);
                     });

    struct RenderedClass {
        std::string key;
        std::string line;
    };
    std::vector<RenderedClass> renderedClasses;
    renderedClasses.reserve(classes.size());
    for (const InterfaceClassShape& shape : classes) {
        RenderedClass rec;
        rec.line = classRecord(shape);
        rec.key = shape.label;
        rec.key += '\x1f';
        for (const std::string& member : shape.memberText) {
            rec.key += member;
            rec.key += '\x1e';
        }
        renderedClasses.push_back(std::move(rec));
    }
    std::stable_sort(renderedClasses.begin(), renderedClasses.end(),
                     [](const RenderedClass& a, const RenderedClass& b) {
                         return a.key < b.key;
                     });

    std::string out = "cp3-interface-signature/1\n";
    for (const RenderedEntry& r : renderedEntries) {
        out += r.line;
        out += '\n';
    }
    for (const RenderedClass& r : renderedClasses) {
        out += r.line;
        out += '\n';
    }
    return out;
}

std::uint64_t interfaceHashOf(const std::vector<InterfaceExportEntry>& entries,
                              const std::vector<InterfaceClassShape>& classes) noexcept {
    try {
        return fnv1a64(serializeInterfaceSignature(entries, classes));
    } catch (...) {
        return 0;
    }
}

}  // namespace cp3::cli
