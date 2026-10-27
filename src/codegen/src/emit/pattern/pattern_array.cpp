// =============================================================================
// src/codegen/src/emit/pattern_array.cpp - the array / tuple binding pattern
// lowering (STYLE.md section 8).
//
// The array pattern reads its source by index and the tuple pattern by
// position:
//
//   * an array source: `arrayAt(src, i)` reads the i-th element; a pattern
//     default (`[a = 1]`) reads the element or the default, so a short array
//     does not run off its end;
//   * a tuple source: `std::get<i>(src)` reads the i-th slot (a tuple has a
//     fixed slot count, so a default never fires there).
//
// The object pattern, the record layouts and the pattern sources live in
// emit/patterns.cpp.
// =============================================================================
#include "detail/generator/generator.h"

#include <cstddef>
#include <string>
#include <vector>

#include "cp3/diag/diagnostic_codes.hpp"
#include "detail/generator/cpp_writer.h"
#include "detail/generator/family.h"
#include "detail/generator/mangle.h"
#include "detail/generator/type_map.h"

namespace cp3 {

using namespace detail;

void Generator::unpackArrayPattern(const ArrayPattern& p, const PatternSource& src,
                                   std::string& out, int indent) {
    // The source is an array (read by index) or a tuple (read by position).
    const bool tuple = isTupleType(src.type);
    const bool array = !tuple && src.type.kind == TypeKind::Array &&
                       !src.type.parts.empty();
    if (!array && !tuple) {
        const bool unknownSource =
            src.type.isUnknown() ||
            (src.type.kind == TypeKind::Object && src.type.name == "$ambient");
        if (unknownSource) {
            reportUnsupported(p, diagcode::kTypeMismatch,
                              "type mismatch: cannot destructure a value of "
                              "unknown type; annotate the source");
        } else {
            reportUnsupported(p, diagcode::kTypeMismatch,
                              "type mismatch: an array pattern needs an array "
                              "or tuple source, got '" +
                                  describe(src.type) + "'");
        }
        return;
    }
    if (tuple && p.elements.size() > src.type.parts.size()) {
        reportUnsupported(p, diagcode::kTypeMismatch,
                          "type mismatch: the array pattern has more elements "
                          "than the tuple source");
        return;
    }
    std::size_t index = 0;
    for (const auto& targetPtr : p.elements) {
        if (!targetPtr) {
            ++index;
            continue;
        }
        const Node* target = targetPtr.get();
        const AssignmentPattern* fallback = nullptr;
        if (target->kind == NodeKind::AssignmentPattern) {
            fallback = &static_cast<const AssignmentPattern&>(*target);
        }
        const Type elem = tuple ? src.type.parts[index] : src.type.parts.front();
        const CppInfo ec = classify(elem);
        if (!ec.representable || ec.shape == Shape::Void) {
            reportUnsupported(*target, diagcode::kTypeMismatch,
                              "type mismatch: array element type '" +
                                  describe(elem) +
                                  "' has no C++ representation");
            ++index;
            continue;
        }
        std::string at;
        if (tuple) {
            // A tuple slot is always present: the default never fires.
            needsTuple_ = true;
            at = "std::get<" + std::to_string(index) + ">(" + src.text + ")";
        } else {
            needsTypedArray_ = true;
            at = "::cp3::runtime::arrayAt(" + src.text + ", " +
                 std::to_string(index) + ".0)";
            if (fallback != nullptr) {
                // `[a = 1]`: the default fills the missing element.
                at = "::cp3::runtime::arrayAtOr(" + src.text + ", " +
                     std::to_string(index) + ".0, " + expr(*fallback->right) + ")";
            }
        }
        const Node* bound = fallback != nullptr ? fallback->left.get() : target;
        if (bound == nullptr) {
            ++index;
            continue;
        }
        bindPatternTarget(*bound, PatternSource{at, elem, src.isConst}, out,
                          indent);
        ++index;
    }
    if (!p.rest) return;
    const auto& re = static_cast<const RestElement&>(*p.rest);
    if (!re.argument) {
        reportUnsupported(*p.rest, diagcode::kTypeMismatch,
                          "type mismatch: an array rest pattern must bind a "
                          "name or pattern");
        return;
    }
    Type elem = array ? src.type.parts.front() : Type::undefined();
    if (tuple) {
        // The tail of a tuple is the rest array: unify the remaining slot
        // types (`[number, string]` becomes `Array<number | string>`). An
        // empty tail is an empty array: `number | undefined` is the storable
        // spelling whose out-of-range read is `undefined`.
        bool any = false;
        for (std::size_t i = index; i < src.type.parts.size(); ++i) {
            elem = any ? unify(elem, src.type.parts[i]) : src.type.parts[i];
            any = true;
        }
        if (!any) elem = Type::unionOf({Type::number(), Type::undefined()});
    }
    CppInfo ec;
    if (!arrayElementInfo(elem, ec)) {
        reportUnsupported(*p.rest, diagcode::kTypeMismatch,
                          "type mismatch: array rest element type '" +
                              describe(elem) +
                              "' has no C++ representation");
        return;
    }
    std::string rest;
    if (tuple) {
        needsTuple_ = true;
        needsTypedArray_ = true;
        std::vector<std::string> values;
        for (std::size_t i = index; i < src.type.parts.size(); ++i) {
            values.push_back("std::get<" + std::to_string(i) + ">(" +
                             src.text + ")");
        }
        rest = "::cp3::runtime::makeArray<" + ec.cpp + ">(std::vector<" +
               ec.cpp + ">{" + joinStrings(values, ", ") + "})";
    } else {
        needsTypedArray_ = true;
        rest = "::cp3::runtime::makeArray<" + ec.cpp + ">(std::vector<" +
               ec.cpp + ">(" + src.text + "->begin() + " +
               std::to_string(index) + ", " + src.text + "->end()))";
    }
    bindPatternTarget(*p.rest, PatternSource{rest, Type::array(elem), src.isConst},
                      out, indent);
}

}  // namespace cp3
