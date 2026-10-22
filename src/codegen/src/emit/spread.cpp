// =============================================================================
// src/codegen/src/emit/spread.cpp - the spread argument / spread element
// lowering (STYLE.md §8).
//
// A spread (`f(...values)`, `[...values, x]`) splices the elements of an
// array into an argument list or an element list. This unit owns the three
// shapes the lowering needs:
//
//   * `spreadListText` - a runtime-built `std::vector<E>`: the parts in
//     order, each either an element expression or a spread of an array. The
//     callers wrap the vector (a cp3 array, a typed view, a promise list).
//   * `spreadFirstText` - the first element of a spread source: `f(...values)`
//     in a one-parameter call takes the first element, and the extra elements
//     are dropped exactly as JS drops the extra arguments.
//   * `spreadArrayText` - the spread source itself, when it is an array whose
//     elements have the wanted C++ spelling.
//
// A spread whose source is not an array (or holds the wrong elements) is a
// type error, not a silent skip: the program cannot be lowered, and the
// diagnostic says why.
// =============================================================================
#include "detail/generator.h"

#include <cstddef>
#include <string>
#include <vector>

#include "detail/type_map.h"

namespace cp3 {

using namespace detail;

std::string Generator::spreadArrayText(const SpreadElement& sp,
                                        const std::string& wantCpp) {
    if (!sp.argument) return std::string();
    const Type st = effectiveType(*sp.argument);
    if (st.kind != TypeKind::Array || st.parts.empty()) return std::string();
    const CppInfo ec = classify(st.parts.front());
    if (!ec.representable || ec.shape == Shape::Void) return std::string();
    if (!wantCpp.empty() && ec.cpp != wantCpp) return std::string();
    return expr(*sp.argument);
}

std::string Generator::spreadFirstText(const SpreadElement& sp,
                                       const std::string& wantCpp) {
    if (!sp.argument) return std::string();
    const Type st = effectiveType(*sp.argument);
    if (st.kind != TypeKind::Array || st.parts.empty()) return std::string();
    const CppInfo ec = classify(st.parts.front());
    if (!ec.representable || ec.shape == Shape::Void) return std::string();
    if (!wantCpp.empty() && ec.cpp != wantCpp) return std::string();
    // The element read is the array helper (bounds-checked, so an empty
    // spread source panics instead of reading out of range).
    needsTypedArray_ = true;
    return "::cp3::runtime::arrayAt<" + ec.cpp + ">(" + expr(*sp.argument) +
           ", 0.0)";
}

std::string Generator::spreadListText(const Type& elemType,
                                      const ExprList& parts) {
    const CppInfo ec = classify(elemType);
    if (!ec.representable) return std::string();
    const std::string v = freshLabel("elems_");
    std::string body = "std::vector<" + ec.cpp + "> " + v + ";";
    for (const auto& part : parts) {
        if (!part) continue;
        if (part->kind != NodeKind::SpreadElement) {
            body += " " + v + ".push_back(" + expr(*part) + ");";
            continue;
        }
        const auto& sp = static_cast<const SpreadElement&>(*part);
        const std::string src = spreadArrayText(sp, ec.cpp);
        if (src.empty()) {
            reportTypeError(*part, "type mismatch: a spread element must be an "
                                   "array of '" + describe(elemType) + "'");
            continue;
        }
        body += " { const auto& cp3_src = " + src + "; " + v + ".insert(" + v +
                ".end(), cp3_src->begin(), cp3_src->end()); }";
    }
    return "[&]{ " + body + " return " + v + "; }()";
}

}  // namespace cp3
