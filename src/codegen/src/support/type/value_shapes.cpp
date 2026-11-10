// =============================================================================
// src/codegen/src/support/value_shapes.cpp - PRIVATE value-shape classification
// (primitive unions + `$o{...}` object value shapes).
//
// The two non-primitive shapes M1 gains here, kept out of type_map.cpp (which
// stays at its size cap):
//
//   * a union over primitive members (`number | string`, also with `boolean`)
//     lowers to the canonical `std::variant<double, std::string[, bool]>`
//     (member order Number -> String -> Bool, so the spelling is stable
//     whatever order the annotation used);
//   * a `$o{...}` object shape whose every field is a spellable value lowers
//     to a generated struct `cp3_obj_<fnv1a(label)>` held through the
//     reference model (`Ref<struct>`, spec 10): objects are reference types, so
//     `let b = a` shares the object. The shape's fields become public members
//     with a `cp3ToText()`/`cp3Truthy()` pair for the formatter and truthiness
//     dispatchers.
//
// The struct texts are registered here (idempotently, keyed by shape label)
// and re-emitted by `objectDeclsFor` for the shapes a unit actually names; the
// label -> name mapping is a FNV-1a hash, so every module spells the same
// shape identically across an import graph.
// =============================================================================
#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "detail/generator/mangle.h"
#include "detail/generator/type_map.h"

namespace cp3 {
namespace detail {

namespace {

// The generated struct name for a shape label: `cp3_obj_` + 16 FNV-1a hex
// digits.
std::string structNameFor(const std::string& label) {
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned char c : label) {
        hash ^= static_cast<std::uint64_t>(c);
        hash *= 1099511628211ull;
    }
    static const char* const kDigits = "0123456789abcdef";
    std::string out = "cp3_obj_";
    for (int shift = 60; shift >= 0; shift -= 4) {
        out.push_back(kDigits[(hash >> shift) & 0xf]);
    }
    return out;
}

bool isIdentifier(const std::string& name) {
    if (name.empty()) return false;
    const auto start = static_cast<unsigned char>(name.front());
    if (!(start == '_' || (start >= 'a' && start <= 'z') ||
          (start >= 'A' && start <= 'Z'))) {
        return false;
    }
    for (unsigned char c : name) {
        if (!(c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9'))) {
            return false;
        }
    }
    return true;
}

// Parses one sema structural type key (`number||`, `array||<number||>`,
// `object|$o{...}|`, `union||<...>`) back into a cp3 Type. The object arm keeps
// the label; `classify` later walks it, which is how nested record and array
// fields keep their exact C++ spelling.
bool parseTypeKey(const std::string& s, std::size_t& pos, Type& out);

// Reads a balanced `$o{...}` or `$t[...]` label starting at `pos`.
bool readShapeLabel(const std::string& s, std::size_t& pos, std::string& out) {
    if (s.compare(pos, 3, "$o{") != 0 && s.compare(pos, 3, "$t[") != 0) {
        return false;
    }
    const char open = s[pos + 2];
    const char close = open == '{' ? '}' : ']';
    const std::size_t start = pos;
    int depth = 0;
    for (; pos < s.size(); ++pos) {
        if (s[pos] == open) {
            ++depth;
        } else if (s[pos] == close) {
            --depth;
            if (depth == 0) {
                ++pos;
                out = s.substr(start, pos - start);
                return true;
            }
        }
    }
    return false;
}

bool parseTypeKey(const std::string& s, std::size_t& pos, Type& out) {
    const std::size_t kindEnd = s.find('|', pos);
    if (kindEnd == std::string::npos) return false;
    const std::string kind = s.substr(pos, kindEnd - pos);
    pos = kindEnd + 1;
    std::string name;
    if (kind == "object") {
        if (!readShapeLabel(s, pos, name)) return false;
        if (pos >= s.size() || s[pos] != '|') return false;
        ++pos;
    } else {
        const std::size_t nameEnd = s.find('|', pos);
        if (nameEnd == std::string::npos) return false;
        name = s.substr(pos, nameEnd - pos);
        pos = nameEnd + 1;
    }
    std::vector<Type> parts;
    while (pos < s.size() && s[pos] == '<') {
        ++pos;
        Type part;
        if (!parseTypeKey(s, pos, part)) return false;
        if (pos >= s.size() || s[pos] != '>') return false;
        ++pos;
        parts.push_back(std::move(part));
    }
    if (kind == "number") {
        out = Type::number();
    } else if (kind == "string") {
        out = Type::string();
    } else if (kind == "boolean") {
        out = Type::boolean();
    } else if (kind == "undefined") {
        out = Type::undefined();
    } else if (kind == "null") {
        out = Type::nullType();
    } else if (kind == "void") {
        out = Type::voidType();
    } else if (kind == "unknown") {
        out = Type::unknown();
    } else if (kind == "error") {
        out = Type::error();
    } else if (kind == "array") {
        if (parts.size() != 1) return false;
        out = Type::array(std::move(parts.front()));
    } else if (kind == "union") {
        out = Type::unionOf(std::move(parts));
    } else if (kind == "nullable") {
        if (parts.size() != 1) return false;
        out = Type::nullable(std::move(parts.front()));
    } else if (kind == "object") {
        out = Type::named(TypeKind::Object, std::move(name));
    } else if (kind == "enum") {
        out = Type::named(TypeKind::Enum, std::move(name));
    } else if (kind == "class") {
        out = Type::named(TypeKind::Class, std::move(name));
        out.parts = std::move(parts);
    } else {
        return false;  // literals/functions are not annotation field types
    }
    return true;
}

// Parses a `$o{name:key,...}` shape label into its representable fields.
// False when any field is not a value type this wave can spell, or when two
// fields would collide as C++ member names. Nested record and array labels are
// parsed by their own `typeKey`, so composites recurse.
bool parseObjectLabel(const std::string& label, std::vector<ObjectField>& out) {
    const std::string prefix = "$o{";
    if (label.rfind(prefix, 0) != 0 || label.size() < prefix.size() + 1 ||
        label.back() != '}') {
        return false;
    }
    const std::size_t end = label.size() - 1;
    std::vector<ObjectField> fields;
    std::size_t pos = prefix.size();
    while (pos < end) {
        const std::size_t colon = label.find(':', pos);
        if (colon == std::string::npos || colon >= end) return false;
        const std::string name = label.substr(pos, colon - pos);
        // A trailing '?' marks an optional field: the value-shape path spells
        // only the all-present fields, so such a shape belongs to the object
        // model (which keeps the optional field as an `std::optional<T>`).
        if (!isIdentifier(name)) return false;
        pos = colon + 1;
        Type fieldType;
        if (!parseTypeKey(label, pos, fieldType)) return false;
        if (pos >= end || label[pos] != ',') return false;
        ++pos;
        const CppInfo ci = classify(fieldType);
        if (!ci.representable) return false;
        switch (ci.shape) {
            case Shape::Number:
            case Shape::String:
            case Shape::Bool:
            case Shape::Array:
            case Shape::Object:
            case Shape::Union:
            case Shape::Optional:
            case Shape::Maybe:
                break;
            default:
                return false;
        }
        fields.push_back(ObjectField{name, ci.cpp, std::move(fieldType)});
    }
    for (std::size_t i = 0; i < fields.size(); ++i) {
        for (std::size_t j = i + 1; j < fields.size(); ++j) {
            if (mapIdent(fields[i].name) == mapIdent(fields[j].name)) {
                return false;
            }
        }
    }
    out = std::move(fields);
    return true;
}

// One registered struct text: the generated value struct for a shape label.
struct RegisteredShape {
    std::string name;
    std::string text;
    std::vector<ObjectField> fields;
};

// The process-lifetime registry (codegen is single-threaded; re-registering a
// shape is a no-op, so state survives module boundaries harmlessly).
std::vector<RegisteredShape>& registry() {
    static std::vector<RegisteredShape> shapes;
    return shapes;
}

// Labels another family owns (a std/json interface record, say): that family
// gives the shape its own C++ spelling, so the value-shape path must leave it
// alone (collectJsonShapes registers its shapes before the emit walk).
std::unordered_set<std::string>& excludedLabels() {
    static std::unordered_set<std::string> labels;
    return labels;
}

// The rendering expression for one struct member in `cp3ToText`. Composite
// fields cannot go through the primitive `toString` family: a nested record
// renders itself, an array renders its elements, and a value-union renders the
// alternative it currently holds.
std::string fieldTextExpr(const ObjectField& f) {
    const std::string member = mapIdent(f.name);
    const CppInfo ci = classify(f.type);
    switch (ci.shape) {
        case Shape::Object:
            return member + "->cp3ToText()";
        case Shape::Array:
            return "::cp3::runtime::arrayJoin(" + member +
                   ", std::string(\",\"))";
        case Shape::Union:
            return "std::visit([](const auto& cp3_v) { return "
                   "::cp3::runtime::unions::textOf(cp3_v); }, " +
                   member + ")";
        case Shape::Optional:
            return member + ".has_value() ? "
                   "::cp3::runtime::toString(*" + member +
                   ") : std::string(\"null\")";
        default:
            return "::cp3::runtime::toString(" + member + ")";
    }
}

// The generated object struct: members in label order, a D49-style rendering,
// and object truthiness. The struct is the pointee of the `Ref<T>` handle the
// value shape classifies as (spec 10 reference objects); `cp3ToText` names
// only runtime.hpp (`toString`), so the generated header stays self-contained.
std::string structTextFor(const std::string& name,
                          const std::vector<ObjectField>& fields) {
    std::string out = "// A cp3 object value shape (built from its literal).\n";
    out += "struct " + name + " {\n";
    for (const ObjectField& f : fields) {
        out += "    " + f.cpp + " " + mapIdent(f.name) + "{};\n";
    }
    if (!fields.empty()) out += "\n";
    out += "    std::string cp3ToText() const {\n";
    if (fields.empty()) {
        out += "        return std::string(\"{}\");\n";
    } else {
        out += "        std::string cp3_text = std::string(\"{ \");\n";
        for (std::size_t i = 0; i < fields.size(); ++i) {
            if (i != 0) out += "        cp3_text += std::string(\", \");\n";
            out += "        cp3_text += " + fieldTextExpr(fields[i]) + ";\n";
        }
        out += "        cp3_text += std::string(\" }\");\n";
        out += "        return cp3_text;\n";
    }
    out += "    }\n";
    out += "    bool cp3Truthy() const { return true; }\n";
    out += "};\n\n";
    return out;
}

}  // namespace

void objectShapeExclude(const std::string& label) {
    excludedLabels().insert(label);
}

bool objectShapeOf(const Type& t, std::string& structName,
                   std::vector<ObjectField>& fields) {
    if (t.kind != TypeKind::Object) return false;
    if (excludedLabels().count(t.name) != 0) return false;
    std::vector<ObjectField> parsed;
    if (!parseObjectLabel(t.name, parsed)) return false;
    const std::string name = structNameFor(t.name);
    structName = name;
    fields = parsed;
    for (const RegisteredShape& shape : registry()) {
        if (shape.name == name) return true;
    }
    registry().push_back(
        RegisteredShape{name, structTextFor(name, parsed), parsed});
    return true;
}

std::string cloneSpecializationFor(const RegisteredShape& shape) {
    std::string out = "namespace cp3::runtime {\n";
    out += "template <>\nstruct Cloner<" + shape.name + "> {\n";
    out += "    static " + shape.name + " clone(const " + shape.name + "& v) {\n";
    out += "        return " + shape.name + "{";
    for (std::size_t i = 0; i < shape.fields.size(); ++i) {
        if (i != 0) out += ", ";
        out += "::cp3::runtime::clone(v." + mapIdent(shape.fields[i].name) + ")";
    }
    out += "};\n";
    out += "    }\n";
    out += "};\n";
    out += "}  // namespace cp3::runtime\n\n";
    return out;
}

std::string objectGlobalDeclsFor(const std::string& text, bool cloneValues) {
    // A struct body may name another generated shape (a nested record). Emit
    // dependencies first: a depth-first post-order over the registered texts,
    // not a flat name sort, so `inner`'s struct precedes the struct that holds
    // it.
    std::vector<const RegisteredShape*> ordered;
    std::unordered_set<const RegisteredShape*> done;
    std::unordered_set<const RegisteredShape*> visiting;
    const auto visit = [&](const auto& self,
                           const RegisteredShape* shape) -> void {
        if (done.count(shape) != 0 || visiting.count(shape) != 0) return;
        visiting.insert(shape);
        for (const RegisteredShape& other : registry()) {
            if (&other != shape &&
                shape->text.find(other.name) != std::string::npos) {
                self(self, &other);
            }
        }
        visiting.erase(shape);
        done.insert(shape);
        ordered.push_back(shape);
    };
    for (const RegisteredShape& shape : registry()) {
        if (text.find(shape.name) != std::string::npos) visit(visit, &shape);
    }
    std::string out;
    for (const RegisteredShape* shape : ordered) {
        // A shared global definition: the worker TU's header and the entry TU
        // both name the same shape, so the guard keeps one definition per
        // translation unit while the nominal type stays identical across them.
        const std::string guard = upperSnake(shape->name) + "_DEFINED";
        out += "#ifndef " + guard + "\n";
        out += "#define " + guard + "\n";
        out += shape->text;
        if (cloneValues) out += cloneSpecializationFor(*shape);
        out += "#endif  // " + guard + "\n\n";
    }
    return out;
}

CppInfo classifyObjectValue(const Type& t) {
    CppInfo info;
    info.type = t;
    std::string name;
    std::vector<ObjectField> fields;
    if (!objectShapeOf(t, name, fields)) return info;
    info.shape = Shape::Object;
    info.representable = true;
    // Plain objects are reference types (spec 10): the handle shares the
    // object on assignment, argument passing, and field storage.
    info.cpp = "::cp3::runtime::Ref<" + name + ">";
    return info;
}

// `A | B` over primitives -> the canonical `std::variant` (Number, String,
// Bool). A nullish member, a non-primitive member or a single distinct member
// leaves the union to `classifyNullishUnion`.
void classifyValueUnion(const Type& t, CppInfo& info) {
    bool hasNumber = false;
    bool hasString = false;
    bool hasBool = false;
    for (const Type& part : t.parts) {
        if (part.isNullish()) return;
        const CppInfo member = classify(part);
        switch (member.shape) {
            case Shape::Number: hasNumber = true; break;
            case Shape::String: hasString = true; break;
            case Shape::Bool: hasBool = true; break;
            default: return;
        }
    }
    const int distinct = static_cast<int>(hasNumber) +
                         static_cast<int>(hasString) +
                         static_cast<int>(hasBool);
    if (distinct < 2) return;
    std::string cpp = "std::variant<double";
    if (hasString) cpp += ", std::string";
    if (hasBool) cpp += ", bool";
    cpp += ">";
    info.shape = Shape::Union;
    info.representable = true;
    info.cpp = std::move(cpp);
}

// `T | null` / `T | undefined` / `T?` over a primitive or an object value
// shape -> `std::optional<T>`; the two-flavor `T | null | undefined` keeps
// the flavored `Maybe<T>` (primitives only: a value struct has no JS
// `null`/`undefined` distinction to preserve).
// Number-backed enums erase to `long long` at the source level, so they are
// trivial to represent once a declaration exists. Accepting Shape::Enum as a
// valid inner shape for `T | null` / `T | undefined` lets Optional<Enum> /
// Maybe<Enum> keep working (e.g. Array<T>.at() element output).
// Array element reads over an enum array arrive here as Optional<Enum>.
void classifyNullishUnion(const Type& t, CppInfo& info) {
    std::vector<Type> nonNullish;
    for (const Type& part : t.parts) {
        if (!part.isNullish()) nonNullish.push_back(part);
    }
    // A union of only nullish members (`T = undefined` instantiated into
    // `T | undefined`): nothing is ever stored; spell it as Undefined.
    if (nonNullish.empty()) {
        info.shape = Shape::Undefined;
        info.emptyKind = NullishKind::Undefined;
        info.representable = true;
        info.cpp = "cp3::runtime::Undefined";
        return;
    }
    if (nonNullish.size() != 1) return;
    const CppInfo inner = classify(nonNullish.front());
    if (!inner.representable) return;
    const bool primitive =
        isPrimitiveShape(inner.shape) || inner.shape == Shape::Enum;
    const bool object =
        inner.shape == Shape::Object || inner.shape == Shape::ClassRef;
    if (!primitive && !object) return;
    const NullishKind kind = nullishKindOf(t);
    if (kind == NullishKind::Mixed) {
        if (!primitive) return;
        info.shape = Shape::Maybe;
        info.cpp = "cp3::runtime::Maybe<" + inner.cpp + ">";
    } else {
        info.shape = Shape::Optional;
        info.cpp = "std::optional<" + inner.cpp + ">";
    }
    info.emptyKind = kind;
    info.representable = true;
}

// The C++ spelling of the shape field named `property` (false when absent).
// The worker payload check reads it too: a member read over a record shape is
// typed by the shape the channel fixes, not by sema's ambient inference.
bool fieldCppText(const std::vector<ObjectField>& fields,
                  const std::string& property, std::string& out) {
    for (const ObjectField& f : fields) {
        if (f.name == property) {
            out = f.cpp;
            return true;
        }
    }
    return false;
}

// Does `t` denote an object value shape, directly or as the single non-nullish
// member of a nullish option (`{...} | null` -> `std::optional<Struct>`)?
bool objectReceiver(const Type& t, Type& object, bool& optional) {
    if (t.kind == TypeKind::Object) {
        if (!classifyObjectValue(t).representable) return false;
        object = t;
        optional = false;
        return true;
    }
    if (t.isUnion() || t.isNullable()) {
        int count = 0;
        for (const Type& part : t.parts) {
            if (part.isNullish()) continue;
            object = part;
            ++count;
        }
        if (count != 1) return false;
        if (!classifyObjectValue(object).representable) return false;
        optional = true;
        return true;
    }
    return false;
}

}  // namespace detail
}  // namespace cp3
