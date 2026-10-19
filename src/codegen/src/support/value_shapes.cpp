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
//   * a `$o{...}` object shape whose every field is primitive lowers to a
//     generated value struct `cp3_obj_<fnv1a(label)>`; the shape's fields
//     become public members with a `cp3ToText()`/`cp3Truthy()` pair for the
//     formatter and truthiness dispatchers.
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

#include "detail/mangle.h"
#include "detail/type_map.h"

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

// The C++ spelling of one `typeKey` (sema's structural field key), or "" when
// the field is not a primitive this wave represents. A primitive key is the
// kind name, '|', the (empty) type name, and another '|': `number||`,
// `string||`, `boolean||`.
std::string fieldCppFor(const std::string& key) {
    if (key == "number||") return "double";
    if (key == "string||") return "std::string";
    if (key == "boolean||") return "bool";
    return std::string();
}

// Parses a `$o{name:key,...}` shape label into its representable fields.
// False when any field is optional, non-primitive, or not a plain identifier,
// or when two fields would collide as C++ member names.
bool parseObjectLabel(const std::string& label, std::vector<ObjectField>& out) {
    const std::string prefix = "$o{";
    if (label.rfind(prefix, 0) != 0 || label.size() < prefix.size() + 1 ||
        label.back() != '}') {
        return false;
    }
    const std::string body =
        label.substr(prefix.size(), label.size() - prefix.size() - 1);
    std::vector<ObjectField> fields;
    std::size_t pos = 0;
    while (pos < body.size()) {
        const std::size_t comma = body.find(',', pos);
        if (comma == std::string::npos) return false;
        const std::string entry = body.substr(pos, comma - pos);
        pos = comma + 1;
        const std::size_t colon = entry.find(':');
        if (colon == std::string::npos) return false;
        const std::string name = entry.substr(0, colon);
        // A trailing '?' marks an optional field: no member spelling here.
        if (!isIdentifier(name)) return false;
        const std::string cpp = fieldCppFor(entry.substr(colon + 1));
        if (cpp.empty()) return false;
        fields.push_back(ObjectField{name, cpp});
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

// The generated value struct: primitive members in label order, a D49-style
// rendering, and object truthiness. `cp3ToText` names only runtime.hpp
// (`toString`), so the generated header stays self-contained.
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
            out += "        cp3_text += ::cp3::runtime::toString(" +
                   mapIdent(fields[i].name) + ");\n";
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
    registry().push_back(RegisteredShape{name, structTextFor(name, parsed)});
    return true;
}

std::string objectDeclsFor(const std::string& text) {
    std::vector<const RegisteredShape*> hits;
    for (const RegisteredShape& shape : registry()) {
        if (text.find(shape.name) != std::string::npos) hits.push_back(&shape);
    }
    std::sort(hits.begin(), hits.end(),
              [](const RegisteredShape* a, const RegisteredShape* b) {
                  return a->name < b->name;
              });
    std::string out;
    for (const RegisteredShape* shape : hits) out += shape->text;
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
    info.cpp = std::move(name);
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
void classifyNullishUnion(const Type& t, CppInfo& info) {
    std::vector<Type> nonNullish;
    for (const Type& part : t.parts) {
        if (!part.isNullish()) nonNullish.push_back(part);
    }
    if (nonNullish.size() != 1) return;
    const CppInfo inner = classify(nonNullish.front());
    if (!inner.representable) return;
    const bool primitive = isPrimitiveShape(inner.shape);
    const bool object = inner.shape == Shape::Object;
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
