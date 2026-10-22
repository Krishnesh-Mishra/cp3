// =============================================================================
// src/codegen/src/emit/class_method.cpp - the class method member emission.
//
// A method member of a class (spec §15): the declaration inside the class and,
// for a namespace-scope class, the out-of-line definition. A generic method
// (spec §16) lowers to a C++ member function template: inside the class the
// template head names the method's own type parameters, the out-of-line
// definition names the class's parameters first and the method's second.
// =============================================================================
#include "detail/generator.h"

#include <string>
#include <vector>

#include "detail/class_model.h"
#include "detail/cpp_writer.h"
#include "detail/mangle.h"
#include "detail/type_map.h"

namespace cp3 {

using namespace detail;

void Generator::emitClassMethodMember(const ClassModel& cm,
                                      const ClassMemberModel& m,
                                      std::string& out, int indent,
                                      std::string* defs) {
    const MethodDeclaration& md = *m.method;
    const std::string ret = methodReturnType(md, &cm, m.typeParams);
    std::vector<std::string> params;
    for (const auto& p : md.params) {
        if (p) params.push_back(methodParamText(*p, &cm, m.typeParams));
    }
    const std::string name = mapIdent(m.name);
    // The template head of a generic method ("" for an ordinary one).
    const std::string memberHead =
        m.typeParams.empty() ? "" : templateHeadOf(m.typeParams);
    const std::string declText = (md.isStatic ? "static " : "") + ret + " " +
                                 name + "(" + joinStrings(params, ", ") + ")";
    // An abstract method (or a signature without a body) stays a declaration.
    if (md.body == nullptr) {
        if (!memberHead.empty()) line(out, indent, memberHead);
        line(out, indent, declText + ";");
        return;
    }
    if (defs == nullptr) {
        // A local class has no member template (C++): the generic method of a
        // function-local class is reported in the model pass.
        if (!memberHead.empty()) line(out, indent, memberHead);
        line(out, indent, declText + " {");
        out += methodBodyText(md, cm, indent + 1, m.typeParams);
        line(out, indent, "}");
        return;
    }
    if (!memberHead.empty()) line(out, indent, memberHead);
    line(out, indent, declText + ";");
    // The definition lands inside the module namespace, so the declarator is
    // the bare `Class::method` (STYLE: a `RET ::ns::Class::method` declarator
    // misparses as a qualified return type when RET is a class type).
    // The out-of-line definition of a member template carries two template
    // headers: the class template's, then the member template's.
    const std::string defHead =
        (cm.typeParams.empty() ? "" : templateHeadOf(cm.typeParams) + "\n") +
        (m.typeParams.empty() ? "" : templateHeadOf(m.typeParams) + "\n");
    *defs += defHead + ret + " " +
             mapIdent(cm.name) + templateUseOf(cm) + "::" + name + "(" +
             joinStrings(params, ", ") + ") {\n" +
             methodBodyText(md, cm, 1, m.typeParams) + "}\n\n";
}

}  // namespace cp3
