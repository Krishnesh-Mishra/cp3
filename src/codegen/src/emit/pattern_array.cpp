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
#include "detail/generator.h"

#include <cstddef>
#include <string>

#include "detail/cpp_writer.h"
#include "detail/mangle.h"
#include "detail/type_map.h"

namespace cp3 {

using namespace detail;

void Generator::unpackArrayPattern(const ArrayPattern& p, const PatternSource& src,
                                   std::string& out, int indent) {
    // The source is an array (read by index) or a tuple (read by position).
    const bool tuple = isTupleType(src.type);
    const bool array = !tuple && src.type.kind == TypeKind::Array &&
                       !src.type.parts.empty();
    // A tuple has a fixed slot count: a pattern that needs more slots than
    // the tuple has (or a rest tail) has no source to read.
    const bool tupleFits = tuple && p.rest == nullptr &&
                           p.elements.size() <= src.type.parts.size();
    if (!array && !tupleFits) {
        notImplemented(p, "this destructuring source");
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
        if (!ec.representable) {
            notImplemented(p, "this array element type");
            return;
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
    const Type elem = src.type.parts.front();
    const CppInfo ec = classify(elem);
    const std::string rest =
        "::cp3::runtime::makeArray<" + ec.cpp + ">(std::vector<" + ec.cpp +
        ">(" + src.text + "->begin() + " + std::to_string(index) + ", " +
        src.text + "->end()))";
    bindPatternTarget(*p.rest, PatternSource{rest, Type::array(elem), src.isConst},
                      out, indent);
}

}  // namespace cp3
