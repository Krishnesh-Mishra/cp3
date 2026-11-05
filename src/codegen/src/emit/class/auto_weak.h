// =============================================================================
// src/codegen/src/emit/class/auto_weak.h - Stage 2 auto-weak (compiler-
// injected non-owning edge) lookup for class emission.
//
// Sema publishes its per-SCC edge selection in
// SemaResult::cycleEscapeSites (docs/design/auto-weak.md):
//
//   auto-weak Src.field -> Dst : candidate (escape: none)   // preview only
//   auto-weak Src.field -> Dst : proven (escape: none)      // lower as weak
//   auto-weak Src.field -> Dst : rejected (reason: <why>)   // never lower
//
// Codegen honors ONLY `proven`. While sema's proof+fuzzer gate is off it
// emits only `candidate` lines, so codegen output is byte-identical to the
// manual-`weak`-only compiler (report-only default). A `proven` field is
// then emitted exactly like a manual `weak` field: a `WeakRef<T>` member
// (weak-cost zero, no refcount), `readWeak` reads returning `T | undefined`,
// and `WeakRef` stores - so reads need the same narrowing as manual weak.
// =============================================================================
#pragma once

#include <string>

#include "cp3/core/analysis.hpp"
#include "detail/class/class_model.h"

namespace cp3::detail {

// The Dst class name of the proven auto-weak edge for `Src.field`, or ""
// when the field is not a proven compiler-injected edge (candidate,
// rejected, manual, or absent). Class and field names are identifiers, so
// the `.` / ` -> ` / ` : ` separators in the sema line are unambiguous.
inline std::string autoWeakDst(const SemaResult& sema,
                               const std::string& src,
                               const std::string& field) {
    const std::string key = src + "." + field;
    for (const std::string& line : sema.cycleEscapeSites) {
        // "auto-weak Src.field -> Dst : proven (escape: none)"
        if (line.compare(0, 10, "auto-weak ") != 0) continue;
        const std::size_t arrow = line.find(" -> ");
        if (arrow == std::string::npos) continue;
        if (line.compare(10, arrow - 10, key) != 0) continue;
        const std::size_t colon = line.find(" : ", arrow);
        if (colon == std::string::npos) continue;
        const std::size_t stateBegin = colon + 3;
        const std::size_t stateEnd = line.find(' ', stateBegin);
        if (line.compare(stateBegin, stateEnd - stateBegin, "proven") != 0) {
            continue;
        }
        return line.substr(arrow + 4, colon - (arrow + 4));
    }
    return std::string();
}

// As autoWeakDst, but the field may be inherited: walk the receiver's base
// chain so the declaring class's proven edge is found.
inline std::string autoWeakDstFor(const SemaResult& sema,
                                  const ClassModel& cm,
                                  const std::string& field) {
    const ClassModel* k = &cm;
    int guard = 0;
    while (k != nullptr && guard++ < 64) {
        const std::string dst = autoWeakDst(sema, k->name, field);
        if (!dst.empty()) return dst;
        k = k->baseName.empty()
                ? nullptr
                : ModelRegistry::get().classByName(k->baseName);
    }
    return std::string();
}

// The field must lower as a non-owning (WeakRef) member.
inline bool isAutoWeakField(const SemaResult& sema, const ClassModel& cm,
                            const std::string& field) {
    return !autoWeakDstFor(sema, cm, field).empty();
}

}  // namespace cp3::detail
