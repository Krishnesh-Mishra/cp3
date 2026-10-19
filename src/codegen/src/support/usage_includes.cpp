// =============================================================================
// src/codegen/src/support/usage_includes.cpp - usage-based include flags.
//
// OWNERSHIP: codegen module. After the emit walk, the unit's scanned text
// (`decls + defs + mainBody`) decides which family includes and helper blocks
// the generated .cpp carries (D69): the typed-array family, collections,
// buffer, the builtin EventEmitter (D74), the operator wave's bitwise/union
// spellings and the D48 core regex value. The flags themselves live on
// GeneratorState; this unit owns only the scan.
// =============================================================================
#include "detail/generator.h"

#include <string>

namespace cp3 {

using namespace detail;

void Generator::applyUsageIncludes(const std::string& familyUse) {
    // The family include (D69) is needed wherever a generated spelling appears.
    if (familyUse.find("::cp3::runtime::Array") != std::string::npos ||
        familyUse.find("::cp3::runtime::TypedArray") != std::string::npos ||
        familyUse.find("::cp3::runtime::SharedArrayBuffer") !=
            std::string::npos ||
        familyUse.find("::cp3::runtime::DataView") != std::string::npos ||
        familyUse.find("std::variant<") != std::string::npos) {  // union slot (docs 09)
        needsTypedArray_ = true;
    }
    if (familyUse.find("::cp3::runtime::collections::") != std::string::npos) {
        needsCollections_ = true;
    }
    // The nominal std/buffer `Buffer` follows the same rule.
    if (familyUse.find("::cp3::runtime::buffer::Buffer") != std::string::npos) {
        needsBuffer_ = true;
    }
    // The builtin EventEmitter (D74): a module-scope binding or a signature can
    // name `Ref<EventEmitter>`/`Listener` without the emitter having been
    // lowered in this unit's own statements.
    if (familyUse.find("::cp3::runtime::EventEmitter") != std::string::npos ||
        familyUse.find("::cp3::runtime::Listener") != std::string::npos) {
        needsEventEmitter_ = true;
    }
    // The operator wave's spellings: bitwise/shift helpers and the unions.
    if (familyUse.find("::cp3::runtime::bitwise::") != std::string::npos) needsBitwise_ = true;
    if (familyUse.find("std::variant<") != std::string::npos) needsUnion_ = true;
    if (familyUse.find("::cp3::runtime::unions::") != std::string::npos) needsUnion_ = true;
    if (familyUse.find("cp3_obj_") != std::string::npos) needsObject_ = true;
    // The core regex value (D48): the same rule - a parameter or exported
    // binding can name the C++ spelling without a literal having been lowered
    // in this unit's own statements.
    if (familyUse.find("::cp3::runtime::regex::Regex") != std::string::npos) {
        needsRegex_ = true;
    }
}

}  // namespace cp3
