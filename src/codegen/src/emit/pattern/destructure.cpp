// =============================================================================
// src/codegen/src/emit/destructure.cpp - binding patterns (STYLE.md section 8).
//
// Destructuring lowers by first materializing the source value into a local
// (`auto cp3_d<N> = <source>;`) and then emitting one plain binding per bound
// name:
//
//   * an object field reads `((cp3_d0)->f)` (a reference object);
//   * an array element reads `(*(cp3_d0))[i]`;
//   * an array rest slice is a fresh `Array<E>` over the tail
//     (`makeArray<E>(std::vector<E>(it + k, end))`);
//   * a pattern default fills the empty case of an optional value
//     (`has_value() ? *field : default`).
//
// A destructured name that the capture analysis marked a cell gets the
// `Cell<T>` storage too, so the sharing semantics hold for a pattern binding
// exactly as for a plain declarator.
//
// The destructuring *assignment* (`[a, b] = expr`) has its own unit:
// src/codegen/src/emit/destructure_assign.cpp.
// =============================================================================
#include "detail/generator/generator.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "cp3/diag/diagnostic_codes.hpp"
#include "detail/generator/cpp_writer.h"
#include "detail/expr/pattern_emitter.h"
#include "detail/generator/family.h"
#include "detail/generator/mangle.h"
#include "detail/generator/type_map.h"

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
// "" for a key form the shape machinery cannot read. A string or number
// literal is a name even when the source spelled it as a computed key
// (`{ ["x"]: y }`).
std::string keyText(const Expr* key, bool computed) {
    if (!key) return std::string();
    if (key->kind == NodeKind::StringLiteral) {
        return static_cast<const StringLiteral&>(*key).value;
    }
    if (key->kind == NodeKind::NumberLiteral) {
        return static_cast<const NumberLiteral&>(*key).raw;
    }
    if (!computed && key->kind == NodeKind::Identifier) {
        return static_cast<const Identifier&>(*key).name;
    }
    return std::string();
}

// Sema types a tuple rest binding as `unknown[]`; the site type is the precise
// array the walker builds from the tuple tail.
bool isPlaceholderArray(const Type& t) {
    return t.kind == TypeKind::Array &&
           (t.parts.empty() || t.parts.front().isUnknown());
}

// A value that may be null/undefined cannot be destructured.
bool hasNullishMember(const Type& t) {
    if (t.isNullable() || t.isNullish()) return true;
    if (!t.isUnion()) return false;
    for (const Type& part : t.parts) {
        if (part.isNullable() || part.isNullish()) return true;
    }
    return false;
}

// Sema's placeholder for a value with no inferred type (an unannotated
// parameter).
bool isAmbientType(const Type& t) {
    return t.kind == TypeKind::Object && t.name == "$ambient";
}

bool isUnknownSource(const Type& t) {
    return t.isUnknown() || isAmbientType(t);
}

}  // namespace

namespace detail {

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
    // The source is an array (read by index) or a tuple (read by position),
    // exactly as the local declaration lowering reads it (pattern_array.cpp).
    const bool tuple = site.type && isTupleType(*site.type);
    const bool array = !tuple && site.type && site.type->kind == TypeKind::Array &&
                       !site.type->parts.empty();
    if (!array && !tuple) {
        const Type source = site.type.value_or(Type::unknown());
        if (isUnknownSource(source)) {
            g.reportUnsupported(ap, diagcode::kTypeMismatch,
                                "type mismatch: cannot destructure a value of "
                                "unknown type; annotate the source");
        } else {
            g.reportUnsupported(ap, diagcode::kTypeMismatch,
                                "type mismatch: an array pattern needs an array "
                                "or tuple source, got '" +
                                    describe(source) + "'");
        }
        return;
    }
    if (tuple && ap.elements.size() > site.type->parts.size()) {
        g.reportUnsupported(ap, diagcode::kTypeMismatch,
                            "type mismatch: the array pattern has more elements "
                            "than the tuple source");
        return;
    }
    const Type elem = tuple ? Type::unknown() : site.type->parts.front();
    std::size_t index = 0;
    for (const auto& el : ap.elements) {
        if (el) {
            const Type atType = tuple ? site.type->parts[index] : elem;
            std::string access;
            if (tuple) {
                g.needsTuple_ = true;
                access = "std::get<" + std::to_string(index) + ">(" +
                         site.access + ")";
            } else {
                g.needsTypedArray_ = true;
                access = "::cp3::runtime::arrayAt(" + site.access + ", " +
                         std::to_string(index) + ".0)";
                if (el->kind == NodeKind::AssignmentPattern) {
                    // `[a = 1]`: the default fills a missing element.
                    const auto& fallback =
                        static_cast<const AssignmentPattern&>(*el);
                    if (!fallback.left || !fallback.right) {
                        g.reportUnsupported(
                            *el, diagcode::kInternalError,
                            "internal error: an array-pattern default needs "
                            "both a target and a value");
                        ++index;
                        continue;
                    }
                    access = "::cp3::runtime::arrayAtOr(" + site.access + ", " +
                             std::to_string(index) + ".0, " +
                             g.expr(*fallback.right) + ")";
                    walkPattern(*fallback.left, Site{access, atType}, out,
                                indent);
                    ++index;
                    continue;
                }
            }
            walkPattern(*el, Site{access, atType}, out, indent);
        }
        ++index;
    }
    if (ap.rest) {
        const auto& re = static_cast<const RestElement&>(*ap.rest);
        if (!re.argument) {
            g.reportUnsupported(*ap.rest, diagcode::kTypeMismatch,
                                "type mismatch: an array rest pattern must bind "
                                "a name or pattern");
            return;
        }
        Type restElem = array ? elem : Type::undefined();
        if (tuple) {
            // The tail of a tuple is the rest array: unify the remaining slot
            // types. An empty tail is an empty array: `number | undefined` is
            // the storable spelling whose out-of-range read is `undefined`.
            bool any = false;
            for (std::size_t i = index; i < site.type->parts.size(); ++i) {
                restElem =
                    any ? unify(restElem, site.type->parts[i])
                        : site.type->parts[i];
                any = true;
            }
            if (!any) {
                restElem = Type::unionOf({Type::number(), Type::undefined()});
            }
        }
        CppInfo ec;
        if (!arrayElementInfo(restElem, ec)) {
            g.reportUnsupported(*ap.rest, diagcode::kTypeMismatch,
                                "type mismatch: array rest element type '" +
                                    describe(restElem) +
                                    "' has no C++ representation");
            return;
        }
        std::string restText;
        if (tuple) {
            g.needsTuple_ = true;
            g.needsTypedArray_ = true;
            std::vector<std::string> values;
            for (std::size_t i = index; i < site.type->parts.size(); ++i) {
                values.push_back("std::get<" + std::to_string(i) + ">(" +
                                 site.access + ")");
            }
            restText = "::cp3::runtime::makeArray<" + ec.cpp + ">(std::vector<" +
                       ec.cpp + ">{" + joinStrings(values, ", ") + "})";
        } else {
            g.needsTypedArray_ = true;
            // The tail slice begins at `index`, but a short or empty source
            // has no element there: clamp to the source's size (JS
            // `src.slice`), which is an empty rest array - `begin() + index`
            // past the end is out of bounds.
            const std::string at = "(*(" + site.access + "))";
            restText =
                "::cp3::runtime::makeArray<" + ec.cpp + ">(std::vector<" +
                ec.cpp + ">(" + at +
                ".begin() + (std::size_t(" + std::to_string(index) + ") < " +
                at + ".size() ? std::size_t(" + std::to_string(index) +
                ") : " + at + ".size()), " + at + ".end()))";
        }
        walkPattern(*re.argument, Site{restText, Type::array(restElem)}, out,
                    indent);
    }
}

void PatternEmitter::walkObjectPattern(const ObjectPattern& op, const Site& site,
                                       std::string& out, int indent) {
    if (!site.type) {
        g.reportUnsupported(op, diagcode::kTypeMismatch,
                            "type mismatch: cannot destructure a value of "
                            "unknown type; annotate the source");
        return;
    }
    if (hasNullishMember(*site.type)) {
        g.reportUnsupported(op, diagcode::kTypeMismatch,
                            "type mismatch: cannot destructure a possibly null "
                            "or undefined value");
        return;
    }
    std::string structName;
    std::vector<std::string> recordFields;
    // The class / interface / plain-object models own the value spelling when
    // they materialized (spec 15): an interface is a by-value wrapper with
    // accessor methods, a class or plain object is a `Ref<T>` handle. The
    // std/json record keeps `.field`.
    const Type base = stripNullish(*site.type);
    const ObjectModel* om = objectModelOf(base);
    if (om != nullptr && !objectModelUsed(*om)) om = nullptr;
    const ClassModel* cm = classModelOf(base);
    std::vector<ObjectField> shapeFields;
    const bool plainShape = objectShapeOf(base, structName, shapeFields);
    const bool record =
        !om && !cm && !plainShape &&
        g.recordLayout(*site.type, structName, recordFields);
    if (!om && !cm && !plainShape && !record) {
        if (isUnknownSource(*site.type)) {
            g.reportUnsupported(op, diagcode::kTypeMismatch,
                                "type mismatch: cannot destructure a value of "
                                "unknown type; annotate the source");
        } else {
            g.reportUnsupported(op, diagcode::kTypeMismatch,
                                "type mismatch: cannot destructure a value of "
                                "type '" +
                                    describe(*site.type) + "'");
        }
        return;
    }
    const auto fieldType = [&](const std::string& name) -> std::optional<Type> {
        if (om != nullptr) {
            const ObjectFieldModel* f = findObjectField(*om, name);
            if (f == nullptr) return std::nullopt;
            return f->optional ? Type::nullable(f->type) : f->type;
        }
        if (cm != nullptr) {
            const ClassMemberModel* m = findClassMember(*cm, name);
            if (m == nullptr || m->isMethod || m->ctor != nullptr) {
                return std::nullopt;
            }
            return m->type;
        }
        if (plainShape) {
            for (const ObjectField& f : shapeFields) {
                if (f.name == name) return f.type;
            }
            return std::nullopt;
        }
        for (const std::string& f : recordFields) {
            if (f == name) return Type::unknown();
        }
        return std::nullopt;
    };
    const auto access = [&](const std::string& name) -> std::string {
        if (om != nullptr) {
            return om->isInterface
                       ? "((" + site.access + ")." + mapIdent(name) + "())"
                       : "((" + site.access + ")->" + mapIdent(name) + ")";
        }
        if (cm != nullptr || plainShape) {
            return "((" + site.access + ")->" + mapIdent(name) + ")";
        }
        return "((" + site.access + ")." + mapIdent(name) + ")";
    };
    for (const auto& prop : op.properties) {
        if (!prop) continue;
        if (!prop->value) {
            g.reportUnsupported(*prop, diagcode::kTypeMismatch,
                                "type mismatch: this destructuring property has "
                                "no binding target");
            continue;
        }
        const std::string name = keyText(prop->key.get(), prop->computed);
        if (name.empty()) {
            g.reportUnsupported(
                *prop, diagcode::kTypeMismatch,
                prop->computed
                    ? "type mismatch: a computed destructuring key must be a "
                      "string or number literal"
                    : "type mismatch: this destructuring property has no name");
            continue;
        }
        const std::optional<Type> ft = fieldType(name);
        if (!ft) {
            g.reportUnsupported(*prop, diagcode::kMemberNotFound,
                                "property '" + name +
                                    "' does not exist on type '" +
                                    describe(base) + "'");
            continue;
        }
        walkPattern(*prop->value, Site{access(name), ft}, out, indent);
    }
    if (op.rest) {
        // The rest object: a fresh shape holding the source's fields except
        // the ones the pattern named (the TypeScript Omit).
        const auto& re = static_cast<const RestElement&>(*op.rest);
        if (!re.argument || re.argument->kind != NodeKind::Identifier) {
            g.reportUnsupported(*op.rest, diagcode::kTypeMismatch,
                                "type mismatch: a rest object pattern must bind "
                                "a name");
            return;
        }
        Type restType;
        const std::string value =
            g.restObjectText(op, site.access, *site.type, restType);
        if (value.empty()) {
            g.reportUnsupported(
                *op.rest, diagcode::kTypeMismatch,
                "type mismatch: a rest object pattern needs a source with a "
                "static value shape");
            return;
        }
        walkPattern(*re.argument, Site{value, restType}, out, indent);
    }
}

void PatternEmitter::emitPatternFallback(const Node& p, std::string& out, int indent) {
    // The pattern walker handles every binding-pattern AST shape; this is the
    // broken-invariant arm. The names are still declared so later references
    // keep compiling.
    g.reportUnsupported(p, diagcode::kInternalError,
                        "internal error: this binding pattern has no lowering");
    std::vector<std::string> names;
    collectNames(p, names);
    const std::string tl = globalsThreadLocal ? "thread_local " : "";
    for (const std::string& name : names) {
        if (globals != nullptr) {
            // A module-scope global: the declaration lives in `globals`, and
            // the binding site only assigns it.
            line(*globals, 0, "inline " + tl + "cp3::runtime::Undefined " +
                                  mapIdent(name) + "{};");
            const std::string target =
                globalsAreModuleScope ? g.moduleRef(name) : mapIdent(name);
            line(out, indent, target + " = cp3::runtime::undefined;");
            g.declare(name, Type::unknown(), /*cell=*/false,
                      globalsAreModuleScope);
        } else {
            line(out, indent,
                 "auto " + mapIdent(name) + " = cp3::runtime::undefined;");
            g.declare(name, Type::unknown());
        }
    }
}

// A destructuring declaration with no initializer: every name the pattern
// introduces is bound to `undefined`, the same lowering a plain `let x;`
// gets (the module-variable unit's `noInit`). There is no source value to
// read, so the pattern walk never runs: the names are declared and the
// binding exists, exactly as the source asks.
void PatternEmitter::emitUndefinedBindings(const Node& pattern, std::string& out,
                                           int indent) {
    std::vector<std::string> names;
    collectNames(pattern, names);
    const std::string tl = globalsThreadLocal ? "thread_local " : "";
    for (const std::string& name : names) {
        if (globals != nullptr) {
            // A module-scope global: the declaration lives in `globals`, and
            // the binding site only assigns it.
            line(*globals, 0, "inline " + tl + "cp3::runtime::Undefined " +
                                  mapIdent(name) + "{};");
            const std::string target =
                globalsAreModuleScope ? g.moduleRef(name) : mapIdent(name);
            line(out, indent, target + " = cp3::runtime::undefined;");
            g.declare(name, Type::undefined(), /*cell=*/false,
                      globalsAreModuleScope);
            continue;
        }
        if (g.isCell(name)) {
            // A captured-and-reassigned name keeps the shared-cell storage, so
            // the enclosing frame and every closure share one binding.
            line(out, indent,
                 "::cp3::runtime::Cell<cp3::runtime::Undefined> " +
                     mapIdent(name) + "(cp3::runtime::undefined);");
            g.declare(name, Type::undefined(), true);
            continue;
        }
        line(out, indent,
             "auto " + mapIdent(name) + " = cp3::runtime::undefined;");
        g.declare(name, Type::undefined());
    }
}

// One bound name: the binding line plus the scope entry. Sema's resolved type
// wins, except when the access site keeps an optional the binding must
// preserve (a `{ f }` over an optional field: the C++ value is
// std::optional<T>).
void PatternEmitter::bindPatternName(const Identifier& id, const Site& site,
                                     std::string& out, int indent) {
    std::optional<Type> bt = site.type;
    if (bt && (bt->isUnknown() || bt->isError())) bt.reset();
    if (const Type* st = g.sema_.typeOf(id.nodeId);
        st && !st->isUnknown() && !st->isError()) {
        const bool accessOptional =
            site.type && (site.type->isNullable() || site.type->isNullish());
        const bool semaOptional = st->isNullable() || st->isNullish();
        const bool sitePrecise =
            bt && !isPlaceholderArray(*bt) && !bt->isUnknown() &&
            !bt->isError();
        // A tuple rest binding is typed `unknown[]` by sema; the walker passes
        // the precise tail array as the site type, so it wins.
        if (!(isPlaceholderArray(*st) && sitePrecise) &&
            (!bt || !accessOptional || semaOptional)) {
            bt = *st;
        }
    }
    const CppInfo ci = bt ? classify(*bt) : CppInfo{};
    if (bt && (!ci.representable || ci.shape == Shape::Void)) {
        g.reportUnsupported(id, diagcode::kTypeMismatch,
                            "type mismatch: destructured binding '" + id.name +
                                "' has type '" + describe(*bt) +
                                "' with no C++ representation");
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
    if (globals != nullptr) {
        // A module-scope global: the declaration lives in `globals`, and the
        // binding site only assigns it (the name outlives the frame that
        // created it).
        const std::string tl = globalsThreadLocal ? "thread_local " : "";
        line(*globals, 0, "inline " + tl + spelling + " " + mapIdent(id.name) + "{};");
        const std::string target =
            globalsAreModuleScope ? g.moduleRef(id.name) : mapIdent(id.name);
        line(out, indent, target + " = " + site.access + ";");
        g.declare(id.name, bt, /*cell=*/false, globalsAreModuleScope);
        return;
    }
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
    // A defaulted destructuring parameter (`{ a } = { a: 1 }`) needs no
    // special handling here: the C++ parameter carries the default (a C++
    // default argument, emit/patterns.cpp's parameterText), so the bindings
    // read the parameter itself - the default value is already in place.
    emitBindings(*p.name, "cp3_param_" + std::to_string(index),
                 g.parameterType(p, fd, index), body, 1);
}

}  // namespace detail

// One destructuring parameter: the C++ parameter is the generated handle
// `parameterText` spelled (emit/patterns.cpp), so the bindings are emitted at
// the top of the body.
void Generator::emitDestructuringParam(const Parameter& p,
                                       const FunctionDeclaration& fd,
                                       std::size_t index, std::string& body) {
    PatternEmitter(*this).emitParam(p, fd, index, body);
}

}  // namespace cp3
