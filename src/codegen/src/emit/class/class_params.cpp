// =============================================================================
// src/codegen/src/emit/class_params.cpp - the class member parameter lowering
// (STYLE.md section 8).
//
// A class method (or constructor) with a rest parameter takes the trailing
// arguments as one array (spec 15): the fixed parameters before it keep their
// positional arguments, and the rest of the call is packed into the array the
// parameter takes.
//
// The parameter spellings themselves live in emit/class_decl.cpp (the struct
// side of the class surface).
// =============================================================================
#include "detail/generator/generator.h"

#include <cstddef>
#include <string>
#include <vector>

#include "detail/class/class_model.h"
#include "detail/generator/type_map.h"

namespace cp3 {

using namespace detail;

// The argument list of a class method call: a rest parameter takes the
// trailing arguments as one array (spec 15).
std::vector<std::string> Generator::classMethodArgs(
    const CallExpr& c, const ClassMemberModel& mem) {
    if (mem.method == nullptr) return emitPlainArgs(c);
    return classRestArgs(emitPlainArgs(c), &mem.method->params);
}

std::vector<std::string> Generator::classRestArgs(
    const std::vector<std::string>& args, const ParamList* params) {
    if (params == nullptr) return args;
    std::size_t restAt = params->size();
    for (std::size_t i = 0; i < params->size(); ++i) {
        if ((*params)[i] && (*params)[i]->isRest) {
            restAt = i;
            break;
        }
    }
    if (restAt >= params->size()) return args;
    const Parameter* rp = (*params)[restAt].get();
    if (rp == nullptr || !rp->type) return args;
    const Type t = modelTypeFromTypeNode(*rp->type);
    if (t.kind != TypeKind::Array || t.parts.empty()) return args;
    const CppInfo ec = classify(t.parts.front());
    if (!ec.representable || ec.shape == Shape::Void) return args;
    std::vector<std::string> out;
    for (std::size_t i = 0; i < restAt && i < args.size(); ++i) {
        out.push_back(args[i]);
    }
    std::string elems;
    for (std::size_t i = restAt; i < args.size(); ++i) {
        if (i != restAt) elems += ", ";
        elems += args[i];
    }
    out.push_back("::cp3::runtime::makeArray<" + ec.cpp + ">(std::vector<" +
                  ec.cpp + ">{" + elems + "})");
    return out;
}

}  // namespace cp3
