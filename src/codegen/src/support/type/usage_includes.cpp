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
#include "detail/generator/generator.h"

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
    // The std/*/types facade modules lower their nominal classes onto the
    // runtime spellings without ever touching a runtime API, so the family
    // headers must follow the same scan (no emitter ever sets these flags
    // for an annotation-only program).
    if (familyUse.find("::cp3::runtime::url::") != std::string::npos) {
        needsUrl_ = true;
    }
    if (familyUse.find("::cp3::runtime::text::") != std::string::npos) {
        needsText_ = true;
    }
    if (familyUse.find("::cp3::runtime::date::") != std::string::npos) {
        needsDate_ = true;
    }
    if (familyUse.find("::cp3::runtime::time::") != std::string::npos) {
        needsTime_ = true;
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
    // The spec 15 object surface: `Ref<T>` / `WeakRef<T>` come from
    // cp3/core/value.hpp, which runtime.hpp does not drag in. The cycle
    // collector headers ride along with them.
    if (familyUse.find("::cp3::runtime::Ref<") != std::string::npos ||
        familyUse.find("::cp3::runtime::WeakRef<") != std::string::npos ||
        familyUse.find("::cp3::runtime::CycleRef<") != std::string::npos ||
        familyUse.find("::cp3::runtime::CycleWeakRef<") != std::string::npos ||
        familyUse.find("makeRef") != std::string::npos ||
        familyUse.find("makeCycleRef") != std::string::npos ||
        familyUse.find("cp3_cycle_visit") != std::string::npos ||
        familyUse.find("std::shared_ptr<void>") != std::string::npos) {
        needsRef_ = true;
    }
    // The core regex value (D48): the same rule - a parameter or exported
    // binding can name the C++ spelling without a literal having been lowered
    // in this unit's own statements.
    if (familyUse.find("::cp3::runtime::regex::Regex") != std::string::npos) {
        needsRegex_ = true;
    }
}

}  // namespace cp3
