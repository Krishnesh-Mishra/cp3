// =============================================================================
// src/codegen/src/emit/class_shape.cpp - structural object / interface struct
// emission for the spec §15 object surface (STYLE.md §8).
//
// `objectStructsText` materializes one C++ struct per usable registered shape:
//   * a fields-only interface also gets a literal struct, so `{ x: 1 }` can be
//     constructed with `makeRef<cp3_litN>(...)` and converted through the
//     wrapper's converting constructor;
//   * an interface gets a wrapper value (a `shared_ptr<void>` payload plus one
//     thunk per member) whose converting constructor adapts any implementing
//     class handle, which is what makes structural typing work in generated
//     C++;
//   * a plain object gets a field struct behind `Ref<cp3_objN>`.
// A shape whose members have no C++ spelling is not emitted; `classify`
// reports it as not representable, and the use site reports a real diagnostic
// instead of lowering the value.
// =============================================================================
#include "detail/class/class_model.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "detail/generator/cpp_writer.h"
#include "detail/generator/mangle.h"
#include "detail/generator/type_map.h"

namespace cp3 {
namespace detail {

namespace {

// The classify spelling of a function type's parameter / return type.
std::string fnParamCpp(const Type& fn, std::size_t index) {
    if (index >= fn.parts.size()) return "double";
    const CppInfo ci = classify(fn.parts[index]);
    return ci.representable ? ci.cpp : "double";
}

std::string fnReturnCpp(const Type& fn) {
    if (fn.parts.empty()) return "void";
    const CppInfo ci = classify(fn.parts.back());
    return ci.representable ? ci.cpp : "void";
}

// The label of the interface whose wrapper is a by-value member of `f`; "" for
// a handle-typed / non-model field.
std::string wrapperLabelOf(const ObjectFieldModel& f) {
    if (f.isFunction) return {};
    const Type t = stripNullish(f.type);
    if (t.kind != TypeKind::Object) return {};
    const ObjectModel* m = ModelRegistry::get().objectByLabel(t.name);
    if (m == nullptr || !m->isInterface) return {};
    return t.name;
}

// Is every by-value wrapper member of `om` already emitted?
bool depsDone(const ObjectModel& om,
              const std::vector<const ObjectModel*>& all,
              const std::vector<bool>& done) {
    for (const ObjectFieldModel& f : om.fields) {
        const std::string dep = wrapperLabelOf(f);
        if (dep.empty()) continue;
        for (std::size_t j = 0; j < all.size(); ++j) {
            if (!done[j] && all[j]->label == dep) return false;
        }
    }
    return true;
}

std::string fieldDecl(const ObjectFieldModel& f) {
    return objectFieldCpp(f) + " " + mapIdent(f.name) + "{};";
}

// The literal struct of a fields-only interface: plain fields, so an object
// literal can be built with `makeRef<cp3_litN>(args)`.
std::string literalText(const ObjectModel& om) {
    std::string out;
    line(out, 0, "struct " + om.literalName + " {");
    for (const ObjectFieldModel& f : om.fields) {
        if (!f.isMethod) line(out, 1, fieldDecl(f));
    }
    line(out, 0, "};");
    line(out, 0, "");
    return out;
}

// One ctor-init entry per member: the field thunk reads the implementing
// object's field, the method thunk calls its method.
std::string wrapperThunkInit(const ObjectFieldModel& f) {
    const std::string fn = mapIdent(f.name);
    if (f.isMethod) {
        std::string sig = "[](void* p";
        std::string call = "static_cast<T*>(p)->" + fn + "(";
        for (std::size_t i = 0; i + 1 < f.type.parts.size(); ++i) {
            sig += ", " + fnParamCpp(f.type, i) + " a" + std::to_string(i);
            if (i != 0) call += ", ";
            call += "a" + std::to_string(i);
        }
        call += ")";
        return "cp3_m_" + fn + "(" + sig + ") -> " + fnReturnCpp(f.type) +
               " { return " + call + "; })";
    }
    return "cp3_f_" + fn + "([](const void* p) -> " + objectFieldCpp(f) +
           " { return static_cast<const T*>(p)->" + fn + "; })";
}

// The interface wrapper: a shared payload plus one thunk / accessor per member,
// and a converting constructor that adapts any implementing handle.
std::string wrapperText(const ObjectModel& om) {
    const std::string name = om.wrapper;
    std::string out;
    line(out, 0, "struct " + name + " {");
    line(out, 1, "std::shared_ptr<void> cp3_obj;");
    for (const ObjectFieldModel& f : om.fields) {
        const std::string fn = mapIdent(f.name);
        if (!f.isMethod) {
            const std::string type = objectFieldCpp(f);
            line(out, 1, type + " (*cp3_f_" + fn +
                             ")(const void*) = nullptr;");
            line(out, 1, "void (*cp3_s_" + fn + ")(void*, " + type +
                             ") = nullptr;");
            continue;
        }
        std::string sig = fnReturnCpp(f.type) + " (*cp3_m_" + fn + ")(void*";
        for (std::size_t i = 0; i + 1 < f.type.parts.size(); ++i) {
            sig += ", " + fnParamCpp(f.type, i);
        }
        line(out, 1, sig + ") = nullptr;");
    }
    line(out, 1, name + "() = default;");
    line(out, 1, "template <class T>");
    line(out, 1, name + "(const ::cp3::runtime::Ref<T>& obj)");
    line(out, 2, ": cp3_obj(obj)");
    for (const ObjectFieldModel& f : om.fields) {
        line(out, 2, ", " + wrapperThunkInit(f));
        if (!f.isMethod) {
            const std::string fn = mapIdent(f.name);
            line(out, 2, ", cp3_s_" + fn + "([](void* p, " +
                             objectFieldCpp(f) + " v) { static_cast<T*>(p)->" +
                             fn + " = std::move(v); })");
        }
    }
    line(out, 2, "{}");
    // A temporary handle (`new C(...)` converted to an interface) is moved
    // into the wrapper: the copy would be destroyed immediately, so its atomic
    // increment is unobservable.
    line(out, 1, "template <class T>");
    line(out, 1, name + "(::cp3::runtime::Ref<T>&& obj)");
    line(out, 2, ": cp3_obj(std::move(obj))");
    for (const ObjectFieldModel& f : om.fields) {
        line(out, 2, ", " + wrapperThunkInit(f));
        if (!f.isMethod) {
            const std::string fn = mapIdent(f.name);
            line(out, 2, ", cp3_s_" + fn + "([](void* p, " +
                             objectFieldCpp(f) + " v) { static_cast<T*>(p)->" +
                             fn + " = std::move(v); })");
        }
    }
    line(out, 2, "{}");
    for (const ObjectFieldModel& f : om.fields) {
        if (f.isMethod) continue;
        const std::string fn = mapIdent(f.name);
        const std::string type = objectFieldCpp(f);
        line(out, 1, type + " " + fn + "() const { return cp3_f_" + fn +
                         "(cp3_obj.get()); }");
        line(out, 1, "void set_" + fn + "(" + type +
                         " v) { cp3_s_" + fn + "(cp3_obj.get(), std::move(v)); }");
    }
    if (const ObjectFieldModel* ts = findObjectField(om, "toString");
        ts != nullptr && ts->isMethod) {
        // The interface declares `toString`, so `cp3_ts` reaches it through the
        // method thunk: the ADL formatter for `console.log(x)` / `${x}`.
        line(out, 1,
             "friend std::string cp3_ts(const " + name + "& v) { return v." +
                 "cp3_m_" + mapIdent(ts->name) + "(v.cp3_obj.get()); }");
    }
    line(out, 0, "};");
    line(out, 0, "");
    return out;
}

// A plain object struct: fields behind a `Ref<cp3_objN>` handle. The
// `cp3ToText()` pair makes the value formattable (`console.log(obj)`), the
// same surface the all-primitive value shape carries.
std::string plainText(const ObjectModel& om) {
    std::string out;
    line(out, 0, "struct " + om.structName + " {");
    for (const ObjectFieldModel& f : om.fields) {
        if (!f.isMethod) line(out, 1, fieldDecl(f));
    }
    line(out, 1, "std::string cp3ToText() const { return std::string("
                  "\"[object Object]\"); }");
    line(out, 0, "};");
    line(out, 0, "");
    return out;
}

}  // namespace

// The dependency-ordered list of registered models: a by-value wrapper member
// must be complete first, so the walk is a dependency sort, not a name sort.
std::vector<const ObjectModel*> orderModels(
    const std::vector<const ObjectModel*>& all,
    const std::function<bool(const ObjectModel&)>& keep) {
    std::vector<const ObjectModel*> ordered;
    std::vector<bool> done(all.size(), false);
    while (ordered.size() < all.size()) {
        bool progress = false;
        for (std::size_t i = 0; i < all.size(); ++i) {
            if (done[i] || !keep(*all[i]) || !depsDone(*all[i], all, done)) continue;
            done[i] = true;
            ordered.push_back(all[i]);
            progress = true;
        }
        if (!progress) break;  // a cycle: fall back to registration order
    }
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (!done[i] && keep(*all[i])) ordered.push_back(all[i]);
    }
    return ordered;
}

std::string objectStructsText() {
    ModelRegistry& reg = ModelRegistry::get();
    const std::vector<const ObjectModel*> all = reg.objects();
    const std::vector<const ObjectModel*> ordered =
        orderModels(all, [](const ObjectModel&) { return true; });
    std::string out;
    for (const ObjectModel* om : ordered) {
        if (!reg.objectUsable(om->label)) continue;
        if (!objectModelUsed(*om)) continue;  // the value-shape path owns it
        if (om->isInterface) {
            // The wrapper goes to the global scope (interfaceGlobalText);
            // the namespace scope keeps the literal struct its implementing
            // TU builds.
            if (!om->literalName.empty()) out += literalText(*om);
            continue;
        }
        out += plainText(*om);
    }
    return out;
}

// The interface wrappers are named identically by every unit of a program and
// are element-wise identical by construction, so a single global definition
// per TU is both legal (ODR: same tokens, same entities) and required for
// the wrapper type to cross a whole-program boundary. The guard keeps one
// definition per TU when a unit hosts both a .cpp and a header copy.
std::string interfaceGlobalText() {
    ModelRegistry& reg = ModelRegistry::get();
    const std::vector<const ObjectModel*> all = reg.objects();
    const std::vector<const ObjectModel*> ordered =
        orderModels(all, [](const ObjectModel& om) { return om.isInterface; });
    std::string out;
    for (const ObjectModel* om : ordered) {
        if (!reg.objectUsable(om->label)) continue;
        if (!objectModelUsed(*om)) continue;  // the value-shape path owns it
        const std::string guard = upperSnake(om->wrapper) + "_DEFINED";
        out += "#ifndef " + guard + "\n";
        out += "#define " + guard + "\n";
        out += wrapperText(*om);
        out += "#endif  // " + guard + "\n\n";
    }
    return out;
}

}  // namespace detail
}  // namespace cp3
