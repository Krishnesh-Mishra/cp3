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

#include "detail/generator/cpp_writer.h"
#include "detail/expr/pattern_emitter.h"
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
    const bool tupleFits =
        tuple && ap.rest == nullptr && ap.elements.size() <= site.type->parts.size();
    if (!array && !tupleFits) {
        emitPatternFallback(ap, out, indent);
        return;
    }
    const Type elem = tuple ? Type::unknown() : site.type->parts.front();
    std::size_t index = 0;
    for (const auto& el : ap.elements) {
        if (el) {
            const Type atType = tuple ? site.type->parts[index] : elem;
            std::string access;
            if (tuple) {
                needsTuple_ = true;
                access = "std::get<" + std::to_string(index) + ">(" +
                         site.access + ")";
            } else {
                needsTypedArray_ = true;
                access = "::cp3::runtime::arrayAt(" + site.access + ", " +
                         std::to_string(index) + ".0)";
                if (el->kind == NodeKind::AssignmentPattern) {
                    // `[a = 1]`: the default fills a missing element.
                    const auto& fallback =
                        static_cast<const AssignmentPattern&>(*el);
                    if (!fallback.left || !fallback.right) {
                        emitPatternFallback(*el, out, indent);
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
        const CppInfo ec = classify(elem);
        if (!re.argument || !ec.representable || ec.shape == Shape::Void) {
            emitPatternFallback(*ap.rest, out, indent);
            return;
        }
        const std::string restText =
            "::cp3::runtime::makeArray<" + ec.cpp + ">(std::vector<" + ec.cpp +
            ">((*(" + site.access + ")).begin() + " + std::to_string(index) +
            ", (*(" + site.access + ")).end()))";
        walkPattern(*re.argument, Site{restText, Type::array(elem)}, out,
                    indent);
    }
}

void PatternEmitter::walkObjectPattern(const ObjectPattern& op, const Site& site,
                                       std::string& out, int indent) {
    std::string structName;
    std::vector<std::string> recordFields;
    // The class / interface / plain-object models own the value spelling when
    // they materialized (spec 15): an interface is a by-value wrapper with
    // accessor methods, a class or plain object is a `Ref<T>` handle. The
    // std/json record keeps `.field`.
    const Type base =
        site.type ? stripNullish(*site.type) : Type::unknown();
    const ObjectModel* om = site.type ? objectModelOf(base) : nullptr;
    if (om != nullptr && !objectModelUsed(*om)) om = nullptr;
    const ClassModel* cm = site.type ? classModelOf(base) : nullptr;
    std::vector<ObjectField> shapeFields;
    const bool plainShape =
        site.type && objectShapeOf(base, structName, shapeFields);
    const bool record =
        !om && !cm && !plainShape && site.type &&
        g.recordLayout(*site.type, structName, recordFields);
    if (!om && !cm && !plainShape && !record) {
        emitPatternFallback(op, out, indent);
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
        if (!prop || prop->computed || !prop->value) {
            emitPatternFallback(op, out, indent);
            return;
        }
        const std::string name = keyText(prop->key.get());
        const std::optional<Type> ft = fieldType(name);
        if (!ft) {
            emitPatternFallback(op, out, indent);
            return;
        }
        walkPattern(*prop->value, Site{access(name), ft}, out, indent);
    }
    if (op.rest) {
        // The rest object: a fresh shape holding the source's fields except
        // the ones the pattern named (the TypeScript Omit).
        const auto& re = static_cast<const RestElement&>(*op.rest);
        if (!re.argument || re.argument->kind != NodeKind::Identifier) {
            g.reportTypeError(*op.rest,
                              "type mismatch: a rest object pattern must bind "
                              "a name");
            return;
        }
        Type restType;
        const std::string value =
            g.restObjectText(op, site.access, *site.type, restType);
        if (value.empty()) {
            g.reportTypeError(*op.rest,
                              "type mismatch: a rest object pattern needs a "
                              "source with known properties");
            return;
        }
        walkPattern(*re.argument, Site{value, restType}, out, indent);
    }
}

void PatternEmitter::emitPatternFallback(const Node& p, std::string& out, int indent) {
    // A pattern the shape machinery cannot read is reported; its names are
    // still declared so later references keep compiling.
    g.notImplemented(p, "this destructuring pattern");
    std::vector<std::string> names;
    collectNames(p, names);
    const std::string tl = globalsThreadLocal ? "thread_local " : "";
    for (const std::string& name : names) {
        if (globals != nullptr) {
            // A module-scope global: the declaration lives in `globals`, and
            // the binding site only assigns it.
            line(*globals, 0, tl + "cp3::runtime::Undefined " +
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
            line(*globals, 0, tl + "cp3::runtime::Undefined " +
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
    if (globals != nullptr) {
        // A module-scope global: the declaration lives in `globals`, and the
        // binding site only assigns it (the name outlives the frame that
        // created it).
        const std::string tl = globalsThreadLocal ? "thread_local " : "";
        line(*globals, 0, tl + spelling + " " + mapIdent(id.name) + "{};");
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
