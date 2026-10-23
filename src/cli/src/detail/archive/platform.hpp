// =============================================================================
// src/cli/src/detail/platform.hpp - platform identity DECLARATIONS.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// Definitions live in src/cli/src/pkg/platform.cpp.
//
// A platform id is either a family name ("windows", "linux", "mac") or an
// arch-qualified form ("windows-x64", "linux-x64", "mac-arm64"). Ids are
// normalized explicitly (aliases folded, case folded), so the same platform is
// always spelled the same way in package.json, in the archive layout and in
// the cache manifests (CLI-README §5.4).
// =============================================================================
#pragma once

#include <string>
#include <vector>

namespace cp3::cli {

// An OS family plus an optional architecture. An empty `arch` is a family-only
// id ("windows"), which every architecture of that family satisfies.
struct PlatformId {
    std::string family;
    std::string arch;

    bool qualified() const noexcept { return !arch.empty(); }
};

// The platform of the machine cp3c runs on ("windows-x64", "linux-x64", ...).
const PlatformId& currentPlatform();

// The canonical spelling of `id`: "windows" or "windows-x64".
std::string platformIdText(const PlatformId& id);

// Parses one platform id, folding the known aliases ("win", "macos",
// "x86_64", "aarch64", ...) and the case. Returns false with a message in
// `err` for an id that names no known platform.
bool parsePlatformId(const std::string& text, PlatformId& out, std::string& err);

// True when a cache built for `have` satisfies the claim `claim`: a
// family-only claim matches every architecture of that family.
bool platformSatisfies(const PlatformId& claim, const PlatformId& have);

// Parses and de-duplicates a list of platform ids, keeping the input order.
std::vector<PlatformId> normalizePlatformIds(const std::vector<std::string>& ids,
                                              std::string& err);

}  // namespace cp3::cli
