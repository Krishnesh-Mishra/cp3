// =============================================================================
// modules/prelude_scan.h — §18 prelude discovery (PRIVATE header).
//
// OWNERSHIP: modules module (src/modules/**). Never included cross-module.
//
// Definitions live in src/prelude_scan.cpp. Rules from
// docs/language-spec/18-modules-imports.md:
//   * Same   (`_prelude.same.cp3`): the file's own folder only.
//   * Strong (`_prelude.cp3`): own folder + every ancestor, outermost first.
//   * Weak   (`_prelude.weak.cp3`): own folder + ancestors, outermost first,
//     but an ancestor's weak prelude is stopped ("walled") when a folder
//     strictly below it (and at or above the file's own folder) contains a
//     prelude file of its own.
// Injection order: Same -> Strong -> Weak (DECISIONS D5/D29).
// =============================================================================
#pragma once

#include <filesystem>
#include <string_view>
#include <vector>

#include "cp3/module.hpp"

namespace cp3::modules {

namespace fs = std::filesystem;

// One discovered prelude file plus the variant it was found as.
struct PreludeRef {
    fs::path path;
    PreludeKind kind = PreludeKind::None;
};

// True when `name` is one of the three prelude file names; sets `kind`.
bool isPreludeFileName(std::string_view name, PreludeKind& kind);

// True when `folder` contains any prelude variant (the weak-injection wall).
bool folderHasPrelude(const fs::path& folder);

// The prelude of `folder` for `kind`, or an empty path when absent.
fs::path preludeInFolder(const fs::path& folder, PreludeKind kind);

// Every folder from the filesystem root down to `folder` (inclusive).
std::vector<fs::path> ancestorChain(const fs::path& folder);

// The preludes that apply to a file in `folder`, in injection order.
std::vector<PreludeRef> preludesForFolder(const fs::path& folder);

}  // namespace cp3::modules
