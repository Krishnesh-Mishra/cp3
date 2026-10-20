// =============================================================================
// src/codegen/src/detail/class_model.h - PRIVATE class / interface / object
// value models for the spec §15 object surface.
//
// Codegen lowers cp3 classes to C++ structs behind `Ref<T>` handles, interface
// types to generated wrapper structs (a shared void handle plus per-member
// thunks), and structural object types to generated field structs behind
// `Ref<T>`. `classify` (support/class_types.cpp) consults the process-global
// `ModelRegistry` for a user class name / a sema object-shape label, so the
// mapping lives here rather than in the type map itself.
//
// The registry is rebuilt for every `Generator::run` (reset + namespace) and
// keyed by:
//   * class name            -> ClassModel   (nominal class type)
//   * sema object label      -> ObjectModel  (structural object / interface)
//   * interface cp3 name     -> sema object label
// The label spelling replicates sema's `typeKey` + `makeObjectType`
// (src/sema/src/checks/type_check.cpp) byte for byte, which is what lets a
// sema-inferred type (`let o = { x: 1 }`) find its generated struct.
// =============================================================================
#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "cp3/ast.hpp"
#include "cp3/types.hpp"

namespace cp3 {
namespace detail {

// One field of a structural object/interface model.
struct ObjectFieldModel {
    std::string name;
    Type type;
    bool optional = false;
    bool isFunction = false;  // an interface method / function-typed field
    bool isMethod = false;    // a MethodSignature: a callable wrapper thunk
};

// A structural object type: a plain field struct (values are `Ref<struct>`),
// or an interface (values are the generated wrapper struct by value, with a
// separate literal struct for object-literal construction).
struct ObjectModel {
    std::string label;        // the sema object-shape label
    std::string structName;   // C++ struct for the fields (plain objects)
    std::string wrapper;      // interface wrapper spelling ("" when plain)
    std::string literalName;  // interface literal struct ("" when not usable)
    bool isInterface = false;
    // Every member has a C++ spelling and a callable thunk form: a member
    // without one keeps the whole shape out of M1 (`classify` treats it as
    // unknown), so the unit reports CP3002 instead of emitting a struct that
    // cannot compile. Computed on demand (nested shapes recurse, and a shape
    // can be registered after its user).
    bool usable = false;
    bool usabilityKnown = false;
    std::vector<ObjectFieldModel> fields;
};

// One class member (field or method). Overloaded methods appear as one entry
// each, in declaration order.
struct ClassMemberModel {
    std::string name;   // cp3 name
    Type type;          // field type, or Type::function(params, ret)
    bool isStatic = false;
    bool isPrivate = false;
    bool isProtected = false;
    bool isWeak = false;
    bool isMethod = false;
    bool isAbstract = false;
    bool hasInitializer = false;
    const PropertyDeclaration* field = nullptr;
    const MethodDeclaration* method = nullptr;
    const ConstructorDeclaration* ctor = nullptr;
    const AccessorDeclaration* accessor = nullptr;
    bool isGetter = false;
};

// A non-generic class declaration lowered to a C++ struct.
struct ClassModel {
    std::string name;     // cp3 name
    std::string cpp;      // qualified struct spelling
    bool local = false;   // declared inside a function body
    const ClassDeclaration* decl = nullptr;
    std::string baseName; // `extends` target ("" for none)
    std::vector<std::string> typeParams;  // generic class parameters
    std::vector<ClassMemberModel> members;
};

// The per-unit model registry (see the file header).
class ModelRegistry {
public:
    static ModelRegistry& get();

    void reset();
    void setNamespace(const std::string& ns);
    const std::string& namesp() const { return namesp_; }

    ClassModel& addClass(const std::string& name, bool local,
                         const ClassDeclaration* decl);
    ClassModel* classByName(const std::string& name);
    const ClassModel* classByName(const std::string& name) const;
    // The classes in declaration order (deterministic struct emission).
    std::vector<ClassModel*> allClasses();

    // Registers (or returns) a plain object struct for `fields`; the struct
    // name is generated. `label` is the sema label when known.
    ObjectModel& ensureObject(const std::string& label,
                              const std::vector<ObjectFieldModel>& fields);

    // Registers an interface: its sema label, cp3 name, wrapper spelling and
    // members. A literal struct is generated when the members are fields only.
    ObjectModel& addInterface(const std::string& name, const std::string& label,
                              const std::vector<ObjectFieldModel>& fields);

    const ObjectModel* objectByLabel(const std::string& label) const;
    const ObjectModel* interfaceByName(const std::string& name) const;
    // The object models in registration order (stable struct-emission order).
    std::vector<const ObjectModel*> objects() const;

    // Is the object model `label` fully lowerable? Computes and caches on
    // demand; a shape that is currently being computed (a self-reference) is
    // optimistically treated as usable.
    bool objectUsable(const std::string& label);

private:
    bool computeUsable(const std::string& label);

    std::string namesp_;
    std::unordered_map<std::string, ClassModel> classes_;
    std::vector<std::string> classOrder_;
    std::unordered_map<std::string, ObjectModel> objects_;
    std::unordered_map<std::string, std::string> ifaceLabelByName_;
    std::vector<std::string> objectOrder_;
    std::unordered_set<std::string> usableInProgress_;
    int objectCounter_ = 0;
    int litCounter_ = 0;
};

// Does this model supply the value spelling? Interfaces always do; a plain
// object model is used only when the all-primitive value-shape path
// (support/value_shapes.cpp) cannot spell the same label.
bool objectModelUsed(const ObjectModel& om);

// sema's `typeKey` (src/sema/src/checks/type_check.cpp), replicated exactly:
// the label spelling is the codegen/sema contract for object shapes.
std::string typeKeyOf(const Type& t);

// sema's `$o{name:typeKey,...}` label for `fields`.
std::string objectLabelOf(const std::vector<ObjectFieldModel>& fields);

// The property key of a member/signature (`x`, `"x"`, `0`); "" when computed.
std::string keyNameOf(const Expr* key);

// The C++ parameter name of one parameter (`cp3_param` when the parameter is
// unnamed), shared by class methods, interface thunks and detached methods.
std::string paramNameOf(const Parameter& p);

// The member named `name`, searching `cm` and then its base chain; `owner` is
// set to the class that declares it.
const ClassMemberModel* findClassMember(const ClassModel& cm,
                                        const std::string& name);
const ClassMemberModel* findClassMemberChain(const ClassModel& cm,
                                             const std::string& name,
                                             const ClassModel** owner);
// The get/set accessor named `name`, searching `cm` and its base chain.
const ClassMemberModel* findClassAccessor(const ClassModel& cm,
                                          const std::string& name,
                                          bool getter);

// The field named `name` of an object/interface model (null when absent).
const ObjectFieldModel* findObjectField(const ObjectModel& om,
                                        const std::string& name);

// The class model of a nominal class type (null when it is not registered).
const ClassModel* classModelOf(const Type& t);

// The object model of a structural object type (null when not registered).
const ObjectModel* objectModelOf(const Type& t);

// The `Ref<T>` spelling of a class.
std::string classRefCpp(const ClassModel& cm);

// Rewrites every Class part named like one of `typeParams` to TypeKind::
// TypeParam, recursing through parts (function signatures, arrays, ...).
void rewriteTypeParams(Type& t, const std::vector<std::string>& typeParams);

// The C++ struct text of every usable registered object/interface shape, in
// dependency order (an interface whose wrapper is a by-value field of another
// shape is emitted first): the interface literal + wrapper structs, then the
// plain object structs (emit/class_shape.cpp).
std::string objectStructsText();

// Does the class (or an ancestor) declare a constructor?
bool hasClassCtor(const ClassModel& cm);

// Does a value of `t` have a `cp3_ts` formatter? Classes and interfaces get a
// generated overload exactly when they expose a `toString` method; used by the
// console/`${...}` narrowing net.
bool refValueFormattable(const Type& t);

// The C++ spelling of one object/interface field, including the optional
// (`T?` / `T | undefined`) wrapper; "" when the field has no spelling.
std::string objectFieldCpp(const ObjectFieldModel& f);

// Does a value of `t` have a `cp3_ts` spelling in the generated unit? The
// helper block covers the primitives and the nullable wrappers over them, and
// the array templates cover arrays of formattable elements; a class instance or
// interface wrapper formats only when the type exposes a `toString` (the
// per-class overload), and a structural object never does. A `false` result
// keeps the unit on the CP3002 path instead of failing C++ overload
// resolution (used by `Generator::tsCall`).
bool valueFormattable(const Type& t);

// Widen (a literal to its base primitive; everything else unchanged) - the
// codegen-side counterpart of sema's private `widen`.
Type widenType(const Type& t);

}  // namespace detail
}  // namespace cp3
