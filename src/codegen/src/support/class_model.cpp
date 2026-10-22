// =============================================================================
// src/codegen/src/support/class_model.cpp - the class/interface/object model
// registry (detail/class_model.h), plus the sema-label spelling helpers.
//
// The registry is process-global because `classify` / `typeFromTypeNode`
// (support/type_map.cpp) are free functions that must resolve user class names
// and object-shape labels without a `Generator` in hand; `Generator::run`
// resets it before every unit and sets the unit namespace.
// =============================================================================
#include "detail/class_model.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "detail/mangle.h"
#include "detail/type_map.h"

namespace cp3 {
namespace detail {

// -----------------------------------------------------------------------------
// Label spelling (sema parity)
// -----------------------------------------------------------------------------

std::string typeKeyOf(const Type& t) {
    std::string s = typeKindName(t.kind);
    s += '|';
    s += t.name;
    s += '|';
    if (t.isLiteral()) {
        switch (t.literalBase) {
            case TypeKind::Number: s += std::to_string(t.numberValue); break;
            case TypeKind::String: s += t.stringValue; break;
            case TypeKind::Boolean: s += t.boolValue ? "T" : "F"; break;
            default: break;
        }
    }
    for (const Type& p : t.parts) {
        s += '<';
        s += typeKeyOf(p);
        s += '>';
    }
    return s;
}

std::string objectLabelOf(const std::vector<ObjectFieldModel>& fields) {
    std::string label = "$o{";
    for (const ObjectFieldModel& f : fields) {
        label += f.name;
        label += f.optional ? "?" : "";
        label += ':';
        label += typeKeyOf(f.type);
        label += ',';
    }
    label += '}';
    return label;
}

Type widenType(const Type& t) {
    return t.isLiteral() ? t.literalBaseType() : t;
}

std::string keyNameOf(const Expr* key) {
    if (key == nullptr) return {};
    if (key->kind == NodeKind::Identifier) {
        return static_cast<const Identifier&>(*key).name;
    }
    if (key->kind == NodeKind::StringLiteral) {
        return static_cast<const StringLiteral&>(*key).value;
    }
    if (key->kind == NodeKind::NumberLiteral) {
        return static_cast<const NumberLiteral&>(*key).raw;
    }
    return {};
}

std::string paramNameOf(const Parameter& p) {
    if (p.name && p.name->kind == NodeKind::Identifier) {
        return mapIdent(static_cast<const Identifier&>(*p.name).name);
    }
    return "cp3_param";
}

// -----------------------------------------------------------------------------
// Member lookup
// -----------------------------------------------------------------------------

const ClassMemberModel* findClassMember(const ClassModel& cm,
                                        const std::string& name) {
    for (const ClassMemberModel& m : cm.members) {
        if (m.name == name) return &m;
    }
    return nullptr;
}

const ClassMemberModel* findClassMemberChain(const ClassModel& cm,
                                             const std::string& name,
                                             const ClassModel** owner) {
    const ClassModel* k = &cm;
    int guard = 0;
    while (k != nullptr && guard++ < 64) {
        const ClassMemberModel* m = findClassMember(*k, name);
        if (m != nullptr) {
            if (owner != nullptr) *owner = k;
            return m;
        }
        k = k->baseName.empty()
                ? nullptr
                : ModelRegistry::get().classByName(k->baseName);
    }
    if (owner != nullptr) *owner = nullptr;
    return nullptr;
}

const ClassMemberModel* findClassAccessor(const ClassModel& cm,
                                          const std::string& name,
                                          bool getter) {
    const ClassModel* k = &cm;
    int guard = 0;
    while (k != nullptr && guard++ < 64) {
        for (const ClassMemberModel& m : k->members) {
            if (m.name == name && m.accessor != nullptr &&
                m.isGetter == getter) {
                return &m;
            }
        }
        k = k->baseName.empty()
                ? nullptr
                : ModelRegistry::get().classByName(k->baseName);
    }
    return nullptr;
}

const ObjectFieldModel* findObjectField(const ObjectModel& om,
                                        const std::string& name) {
    for (const ObjectFieldModel& f : om.fields) {
        if (f.name == name) return &f;
    }
    return nullptr;
}

const ClassModel* classModelOf(const Type& t) {
    if (t.kind != TypeKind::Class) return nullptr;
    return ModelRegistry::get().classByName(t.name);
}

const ObjectModel* objectModelOf(const Type& t) {
    if (t.kind != TypeKind::Object) return nullptr;
    return ModelRegistry::get().objectByLabel(t.name);
}

std::string classRefCpp(const ClassModel& cm) {
    return "::cp3::runtime::Ref<" + cm.cpp + ">";
}

void rewriteTypeParams(Type& t, const std::vector<std::string>& typeParams) {
    if (t.kind == TypeKind::Class) {
        for (const std::string& p : typeParams) {
            if (t.name == p) {
                t.kind = TypeKind::TypeParam;
                return;
            }
        }
    }
    for (Type& part : t.parts) {
        rewriteTypeParams(part, typeParams);
    }
}

// `template <typename T, typename U>` for a list of type parameter names
// ("" for an empty list). A generic method's own parameters are named here.
std::string templateHeadOf(const std::vector<std::string>& names) {
    if (names.empty()) return "";
    std::string out = "template <";
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i != 0) out += ", ";
        out += "typename " + mapIdent(names[i]);
    }
    return out + ">";
}

// `<T, U>` ("" for a non-generic class): the type arguments of a class
// template at a use site.
std::string templateUseOf(const ClassModel& cm) {
    if (cm.typeParams.empty()) return "";
    std::string out = "<";
    for (std::size_t i = 0; i < cm.typeParams.size(); ++i) {
        if (i != 0) out += ", ";
        out += mapIdent(cm.typeParams[i]);
    }
    return out + ">";
}

Type substituteTypeArgs(const Type& t, const std::vector<std::string>& names,
                         const std::vector<Type>& args) {
    if (t.kind == TypeKind::TypeParam) {
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (names[i] != t.name) continue;
            if (i < args.size()) return args[i];
            return t;
        }
        return t;
    }
    Type out = t;
    for (Type& part : out.parts) {
        part = substituteTypeArgs(part, names, args);
    }
    return out;
}

bool hasClassCtor(const ClassModel& cm) {
    const ClassModel* k = &cm;
    int guard = 0;
    while (k != nullptr && guard++ < 64) {
        for (const ClassMemberModel& m : k->members) {
            if (m.ctor != nullptr) return true;
        }
        k = k->baseName.empty()
                ? nullptr
                : ModelRegistry::get().classByName(k->baseName);
    }
    return false;
}

bool refValueFormattable(const Type& t) {
    if (t.kind == TypeKind::Class) {
        const ClassModel* cm = ModelRegistry::get().classByName(t.name);
        if (cm == nullptr) return false;
        const ClassMemberModel* m = findClassMemberChain(*cm, "toString", nullptr);
        // An abstract `toString` (no body) has no generated formatter: the
        // per-class `cp3_ts` overload is emitted only for a defined one.
        return m != nullptr && m->isMethod && m->method != nullptr &&
               m->method->body != nullptr;
    }
    if (t.kind == TypeKind::Object) {
        const ObjectModel* om = ModelRegistry::get().objectByLabel(t.name);
        if (om == nullptr || !om->isInterface) return false;
        const ObjectFieldModel* f = findObjectField(*om, "toString");
        return f != nullptr && f->isMethod;
    }
    return false;
}

std::string objectFieldCpp(const ObjectFieldModel& f) {
    const Type t = f.optional ? Type::nullable(f.type) : f.type;
    const CppInfo ci = classify(t);
    if (!ci.representable) return std::string();
    if (f.optional && ci.shape != Shape::Optional && ci.shape != Shape::Maybe) {
        return std::string();  // the optional wrapper cannot carry this value
    }
    return ci.cpp;
}

namespace {

bool isModelShape(Shape s) {
    return s == Shape::ClassRef || s == Shape::Object;
}

}  // namespace

bool valueFormattable(const Type& raw) {
    const Type t = widenType(raw);
    if (t.isNullable() || t.isUnion()) {
        std::vector<Type> parts;
        for (const Type& p : t.parts) {
            if (!p.isNullish()) parts.push_back(p);
        }
        if (parts.size() != 1) return true;  // not a shape we can judge
        const CppInfo all = classify(t);
        if (all.shape != Shape::Optional && all.shape != Shape::Maybe) {
            return true;
        }
        // Only the primitive inner types have an `std::optional`/`Maybe`
        // formatter; `std::optional<Ref<T>>` (and the array inner types) do
        // not.
        return isPrimitiveShape(classify(parts.front()).shape);
    }
    const CppInfo ci = classify(t);
    if (isModelShape(ci.shape)) return refValueFormattable(t);
    if (ci.shape == Shape::Array) {
        const Type elem = t.parts.empty() ? Type::unknown() : t.parts.front();
        const CppInfo ec = classify(elem);
        if (isModelShape(ec.shape)) return false;
        if (ec.shape == Shape::Array) return valueFormattable(elem);
    }
    return true;
}

// -----------------------------------------------------------------------------
// ModelRegistry
// -----------------------------------------------------------------------------

ModelRegistry& ModelRegistry::get() {
    static ModelRegistry registry;
    return registry;
}

void ModelRegistry::reset() {
    namesp_.clear();
    classes_.clear();
    classOrder_.clear();
    objects_.clear();
    ifaceLabelByName_.clear();
    objectOrder_.clear();
    usableInProgress_.clear();
    objectCounter_ = 0;
    litCounter_ = 0;
}

void ModelRegistry::setNamespace(const std::string& ns) { namesp_ = ns; }

ClassModel& ModelRegistry::addClass(const std::string& name, bool local,
                                    const ClassDeclaration* decl) {
    const auto it = classes_.find(name);
    if (it != classes_.end()) return it->second;
    ClassModel cm;
    cm.name = name;
    cm.local = local;
    cm.decl = decl;
    cm.cpp = local ? mapIdent(name)
                   : "::" + namesp_ + "::" + mapIdent(name);
    classOrder_.push_back(name);
    return classes_.emplace(name, std::move(cm)).first->second;
}

ClassModel* ModelRegistry::classByName(const std::string& name) {
    const auto it = classes_.find(name);
    return it == classes_.end() ? nullptr : &it->second;
}

const ClassModel* ModelRegistry::classByName(const std::string& name) const {
    const auto it = classes_.find(name);
    return it == classes_.end() ? nullptr : &it->second;
}

std::vector<ClassModel*> ModelRegistry::allClasses() {
    std::vector<ClassModel*> out;
    out.reserve(classOrder_.size());
    for (const std::string& name : classOrder_) {
        ClassModel* cm = classByName(name);
        if (cm != nullptr) out.push_back(cm);
    }
    return out;
}

ObjectModel& ModelRegistry::ensureObject(
    const std::string& label, const std::vector<ObjectFieldModel>& fields) {
    const std::string key =
        label.empty() ? objectLabelOf(fields) : label;
    const auto it = objects_.find(key);
    if (it != objects_.end()) {
        return it->second;
    }
    ObjectModel om;
    om.label = key;
    om.structName = "cp3_obj" + std::to_string(objectCounter_++);
    om.fields = fields;
    objectOrder_.push_back(key);
    return objects_.emplace(key, std::move(om)).first->second;
}

ObjectModel& ModelRegistry::addInterface(
    const std::string& name, const std::string& label,
    const std::vector<ObjectFieldModel>& fields, bool generic) {
    ObjectModel& om = ensureObject(label, fields);
    // The first registration of a shape owns its wrapper and its literal
    // struct: a later registration of the same shape (a generic interface is
    // registered by every use site) must not rename them.
    if (!om.isInterface) {
        om.isInterface = true;
        om.wrapper = "cp3_iface_" + mapIdent(name) +
                     (generic ? "_" + std::to_string(ifaceCounter_++) : "");
        // The interface owns this `$o{...}` label: the all-primitive
        // value-shape path (support/value_shapes.cpp) must not claim it.
        objectShapeExclude(om.label);
        bool fieldsOnly = true;
        for (const ObjectFieldModel& f : om.fields) {
            if (f.isFunction) fieldsOnly = false;
        }
        if (fieldsOnly) {
            om.literalName = "cp3_lit" + std::to_string(litCounter_++);
        }
    }
    ifaceLabelByName_[name] = om.label;
    return om;
}

const ObjectModel* ModelRegistry::objectByLabel(const std::string& label) const {
    const auto it = objects_.find(label);
    return it == objects_.end() ? nullptr : &it->second;
}

void ModelRegistry::addGenericInterface(const std::string& name,
                                        GenericInterfaceModel model) {
    genericIfaces_[name] = std::move(model);
}

const GenericInterfaceModel* ModelRegistry::genericInterface(
    const std::string& name) const {
    const auto it = genericIfaces_.find(name);
    return it == genericIfaces_.end() ? nullptr : &it->second;
}

std::vector<const ObjectModel*> ModelRegistry::objects() const {
    std::vector<const ObjectModel*> out;
    out.reserve(objectOrder_.size());
    for (const std::string& label : objectOrder_) {
        const auto it = objects_.find(label);
        if (it != objects_.end()) out.push_back(&it->second);
    }
    return out;
}

const ObjectModel* ModelRegistry::interfaceByName(const std::string& name) const {
    const auto it = ifaceLabelByName_.find(name);
    if (it == ifaceLabelByName_.end()) return nullptr;
    return objectByLabel(it->second);
}

namespace {

// Every member of a lowerable shape needs a C++ spelling; a method also needs
// a callable thunk over its whole signature. A function-typed non-method
// member has neither (D74's Listener cannot carry a value-returning function),
// so the closure-in-object shapes stay on the CP3002 path.
bool fieldLowerable(const ObjectFieldModel& f) {
    if (f.isFunction && !f.isMethod) return false;
    if (f.isMethod) {
        for (const Type& p : f.type.parts) {
            if (!classify(p).representable) return false;
        }
        return true;
    }
    return !objectFieldCpp(f).empty();
}

}  // namespace

bool ModelRegistry::objectUsable(const std::string& label) {
    const auto it = objects_.find(label);
    if (it == objects_.end()) return false;
    if (it->second.usabilityKnown) return it->second.usable;
    // A shape that refers to itself (directly or through a chain) is treated
    // as usable: its members are handles / wrappers, so no member spelling
    // depends on the shape being complete.
    if (!usableInProgress_.insert(label).second) return true;
    bool ok = true;
    for (const ObjectFieldModel& f : it->second.fields) {
        if (!fieldLowerable(f)) {
            ok = false;
            break;
        }
    }
    usableInProgress_.erase(label);
    it->second.usable = ok;
    it->second.usabilityKnown = true;
    return ok;
}

bool objectModelUsed(const ObjectModel& om) {
    if (om.isInterface) return true;
    Type t(TypeKind::Object, om.label);
    std::string structName;
    std::vector<ObjectField> fields;
    return !objectShapeOf(t, structName, fields);
}

}  // namespace detail
}  // namespace cp3
