// =============================================================================
// src/codegen/src/emit/destructure.cpp - binding patterns (STYLE.md section 8).
//
// Destructuring lowers by first materializing the source value into a local
// (`auto cp3_d<N> = <source>;`) and then emitting one plain binding per bound
// name:
//
//   * an object field reads `((cp3_d0).f)` (a value struct);
//   * an array element reads `(*(cp3_d0))[i]`;
//   * an array rest slice is a fresh `Array<E>` over the tail
//     (`makeArray<E>(std::vector<E>(it + k, end))`);
//   * a pattern default fills the empty case of an optional value
//     (`has_value() ? *field : default`).
//
// A destructured name that the capture analysis marked a cell gets the
// `Cell<T>` storage too, so the sharing semantics hold for a pattern binding
// exactly as for a plain declarator.
// =============================================================================
#include "detail/generator.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "detail/cpp_writer.h"
#include "detail/pattern_emitter.h"
#include "detail/mangle.h"
#include "detail/type_map.h"

namespace cp3 {

using namespace detail;

namespace {

// The bound names of a binding pattern, in source order.
void collectNames(const Node& n, std::vector<std::string>& out) {
    switch (n.kind) {
        case NodeKind::Identifier:
            out.push_back(static_cast<const Identifier&>(n).name);
            return;
        case NodeKind::AssignmentPattern: {
            const auto& ap = static_cast<const AssignmentPattern&>(n);
            if (ap.left) collectNames(*ap.left, out);
            return;
        }
        case NodeKind::RestElement: {
            const auto& re = static_cast<const RestElement&>(n);
            if (re.argument) collectNames(*re.argument, out);
            return;
        }
        case NodeKind::ArrayPattern: {
            const auto& ap = static_cast<const ArrayPattern&>(n);
            for (const auto& el : ap.elements) {
                if (el) collectNames(*el, out);
            }
            if (ap.rest) collectNames(*ap.rest, out);
            return;
        }
        case NodeKind::ObjectPattern: {
            const auto& op = static_cast<const ObjectPattern&>(n);
            for (const auto& prop : op.properties) {
                if (prop && prop->value) collectNames(*prop->value, out);
            }
            if (op.rest) collectNames(*op.rest, out);
            return;
        }
        default:
            return;
    }
}

// The member name of a pattern property key (`{ name: n }`, `{ "x": y }`), or
// "" for a key form the shape machinery cannot read.
std::string keyText(const Expr* key) {
    if (!key) return std::string();
    if (key->kind == NodeKind::Identifier) {
        return static_cast<const Identifier&>(*key).name;
    }
    if (key->kind == NodeKind::StringLiteral) {
        return static_cast<const StringLiteral&>(*key).value;
    }
    if (key->kind == NodeKind::NumberLiteral) {
        return static_cast<const NumberLiteral&>(*key).raw;
    }
    return std::string();
}

}  // namespace

namespace detail {

// The Identifier a parameter binds: its own name, or the argument of the
// `RestElement` the parser wraps a rest parameter in. null for a binding
// pattern (its names are bound at the top of the body).
const Identifier* parameterIdentifier(const Parameter& p) {
    const Node* n = p.name.get();
    if (!n) return nullptr;
    if (n->kind == NodeKind::Identifier) {
        return &static_cast<const Identifier&>(*n);
    }
    if (n->kind == NodeKind::RestElement) {
        const auto& re = static_cast<const RestElement&>(*n);
        if (re.argument && re.argument->kind == NodeKind::Identifier) {
            return &static_cast<const Identifier&>(*re.argument);
        }
    }
    return nullptr;
}

// Emits one binding per name the pattern introduces, reading each value from
// `src` (the materialized source local) typed `type`.
void PatternEmitter::emitBindings(const Node& pattern, const std::string& src,
                                  const std::optional<Type>& type, std::string& out,
                                  int indent) {
    walkPattern(pattern, Site{src, type}, out, indent);
}

void PatternEmitter::walkPattern(const Node& p, const Site& site,
                                 std::string& out, int indent) {
    switch (p.kind) {
        case NodeKind::Identifier:
            bindPatternName(static_cast<const Identifier&>(p), site, out, indent);
            return;
        case NodeKind::AssignmentPattern:
            walkAssignmentPattern(static_cast<const AssignmentPattern&>(p), site,
                                  out, indent);
            return;
        case NodeKind::ArrayPattern:
            walkArrayPattern(static_cast<const ArrayPattern&>(p), site, out, indent);
            return;
        case NodeKind::ObjectPattern:
            walkObjectPattern(static_cast<const ObjectPattern&>(p), site, out,
                              indent);
            return;
        default:
            emitPatternFallback(p, out, indent);
            return;
    }
}

void PatternEmitter::walkAssignmentPattern(const AssignmentPattern& ap,
                                           const Site& site, std::string& out,
                                           int indent) {
    if (!ap.left) return;
    Site inner = site;
    if (site.type && ap.right &&
        (site.type->isNullable() || nullishKindOf(*site.type) != NullishKind::None)) {
        // The default fills the empty case of an optional value.
        inner.type = detail::stripNullish(*site.type);
        inner.access = "(((" + site.access + ").has_value()) ? *(" + site.access +
                       ") : (" + g.expr(*ap.right) + "))";
    }
    walkPattern(*ap.left, inner, out, indent);
}

void PatternEmitter::walkArrayPattern(const ArrayPattern& ap, const Site& site,
                                      std::string& out, int indent) {
    std::optional<Type> elem;
    if (site.type && site.type->kind == TypeKind::Array && !site.type->parts.empty()) {
        elem = site.type->parts.front();
    }
    if (!elem) {
        emitPatternFallback(ap, out, indent);
        return;
    }
    std::size_t index = 0;
    for (const auto& el : ap.elements) {
        if (el) {
            const std::string access =
                "((*(" + site.access + "))[" + std::to_string(index) + "])";
            walkPattern(*el, Site{access, elem}, out, indent);
        }
        ++index;
    }
    if (ap.rest) {
        const auto& re = static_cast<const RestElement&>(*ap.rest);
        const CppInfo ec = classify(*elem);
        if (!re.argument || !ec.representable || ec.shape == Shape::Void) {
            emitPatternFallback(*ap.rest, out, indent);
            return;
        }
        const std::string restText =
            "::cp3::runtime::makeArray<" + ec.cpp + ">(std::vector<" + ec.cpp +
            ">((*(" + site.access + ")).begin() + " + std::to_string(index) +
            ", (*(" + site.access + ")).end()))";
        walkPattern(*re.argument, Site{restText, Type::array(*elem)}, out,
                    indent);
    }
}

void PatternEmitter::walkObjectPattern(const ObjectPattern& op, const Site& site,
                                       std::string& out, int indent) {
    std::string structName;
    std::vector<ObjectField> fields;
    if (!site.type || !objectShapeOf(*site.type, structName, fields)) {
        emitPatternFallback(op, out, indent);
        return;
    }
    for (const auto& prop : op.properties) {
        if (!prop || prop->computed || !prop->value) {
            emitPatternFallback(op, out, indent);
            return;
        }
        const std::string name = keyText(prop->key.get());
        const ObjectField* field = nullptr;
        for (const ObjectField& candidate : fields) {
            if (candidate.name == name) {
                field = &candidate;
                break;
            }
        }
        if (name.empty() || !field) {
            emitPatternFallback(op, out, indent);
            return;
        }
        const std::string access = "((" + site.access + ")." + mapIdent(name) + ")";
        walkPattern(*prop->value, Site{access, std::nullopt}, out, indent);
    }
    if (op.rest) emitPatternFallback(*op.rest, out, indent);
}

void PatternEmitter::emitPatternFallback(const Node& p, std::string& out, int indent) {
    // A pattern the shape machinery cannot read stays CP3002; its names are
    // still declared so later references keep compiling.
    g.notImplemented(p, "this destructuring pattern");
    std::vector<std::string> names;
    collectNames(p, names);
    for (const std::string& name : names) {
        line(out, indent,
             "auto " + mapIdent(name) + " = cp3::runtime::undefined;");
        g.declare(name, Type::unknown());
    }
}

// One bound name: the binding line plus the scope entry. Sema's resolved type
// wins, except when the access site keeps an optional the binding must
// preserve (a `{ f }` over an optional field: the C++ value is
// std::optional<T>).
void PatternEmitter::bindPatternName(const Identifier& id, const Site& site,
                                     std::string& out, int indent) {
    std::optional<Type> bt = site.type;
    if (const Type* st = g.sema_.typeOf(id.nodeId);
        st && !st->isUnknown() && !st->isError()) {
        const bool accessOptional =
            site.type && (site.type->isNullable() || site.type->isNullish());
        const bool semaOptional = st->isNullable() || st->isNullish();
        if (!bt || !accessOptional || semaOptional) bt = *st;
    }
    const CppInfo ci = bt ? classify(*bt) : CppInfo{};
    if (bt && !ci.representable) {
        g.notImplemented(id, "this destructured binding");
        line(out, indent,
             "auto " + mapIdent(id.name) + " = cp3::runtime::undefined;");
        g.declare(id.name, Type::unknown());
        return;
    }
    const bool cell = g.isCell(id.name);
    if (cell && ci.representable && ci.shape != Shape::Void) {
        line(out, indent, "::cp3::runtime::Cell<" + ci.cpp + "> " +
                              mapIdent(id.name) + "(" + site.access + ");");
        g.declare(id.name, bt, true);
        return;
    }
    const std::string spelling =
        (ci.representable && ci.shape != Shape::Void) ? ci.cpp : "auto";
    line(out, indent, spelling + " " + mapIdent(id.name) + " = " + site.access +
                          ";");
    g.declare(id.name, bt);
}

// One destructuring parameter: the C++ parameter itself is the anonymous
// `cp3_param<index>` (parameterText), so the bindings are emitted at the top
// of the body.
void PatternEmitter::emitParam(const Parameter& p, const FunctionDeclaration& fd,
                               std::size_t index, std::string& body) {
    // A plain (or rest) identifier parameter is a real C++ parameter already;
    // only a binding pattern lowers bindings at the top of the body.
    if (parameterIdentifier(p)) return;
    if (!p.name) return;
    if (p.defaultValue) g.notImplemented(p, "a defaulted destructuring parameter");
    emitBindings(*p.name, "cp3_param" + std::to_string(index),
                 g.parameterType(p, fd, index), body, 1);
}

}  // namespace detail

}  // namespace cp3
