// =============================================================================
// modules/prelude_scan.cpp — §18 prelude discovery.
//
// OWNERSHIP: modules module (src/modules/**). Implements prelude_scan.h.
// =============================================================================
#include "prelude_scan.h"

#include <algorithm>
#include <string>
#include <system_error>
#include <vector>

namespace cp3::modules {

namespace {

constexpr const char* kSamePrelude = "_prelude.same.cp3";
constexpr const char* kStrongPrelude = "_prelude.cp3";
constexpr const char* kWeakPrelude = "_prelude.weak.cp3";

const char* preludeFileName(PreludeKind kind) noexcept {
    switch (kind) {
        case PreludeKind::Same:   return kSamePrelude;
        case PreludeKind::Strong: return kStrongPrelude;
        case PreludeKind::Weak:   return kWeakPrelude;
        case PreludeKind::None:   break;
    }
    return nullptr;
}

bool regularFile(const fs::path& path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}

}  // namespace

bool isPreludeFileName(std::string_view name, PreludeKind& kind) {
    if (name == kSamePrelude) {
        kind = PreludeKind::Same;
        return true;
    }
    if (name == kStrongPrelude) {
        kind = PreludeKind::Strong;
        return true;
    }
    if (name == kWeakPrelude) {
        kind = PreludeKind::Weak;
        return true;
    }
    return false;
}

bool folderHasPrelude(const fs::path& folder) {
    return regularFile(folder / kSamePrelude) || regularFile(folder / kStrongPrelude) ||
           regularFile(folder / kWeakPrelude);
}

fs::path preludeInFolder(const fs::path& folder, PreludeKind kind) {
    const char* name = preludeFileName(kind);
    if (name == nullptr) return {};
    const fs::path candidate = folder / name;
    return regularFile(candidate) ? candidate : fs::path{};
}

std::vector<fs::path> ancestorChain(const fs::path& folder) {
    std::vector<fs::path> chain;
    for (fs::path probe = folder; !probe.empty();) {
        chain.push_back(probe);
        const fs::path parent = probe.parent_path();
        if (parent == probe) break;
        probe = parent;
    }
    std::reverse(chain.begin(), chain.end());
    return chain;
}

std::vector<PreludeRef> preludesForFolder(const fs::path& folder) {
    std::vector<PreludeRef> refs;

    // Same: the file's own folder only.
    const fs::path same = preludeInFolder(folder, PreludeKind::Same);
    if (!same.empty()) refs.push_back({same, PreludeKind::Same});

    const std::vector<fs::path> chain = ancestorChain(folder);

    // Strong: every ancestor (own folder included), outermost first.
    for (const fs::path& dir : chain) {
        const fs::path strong = preludeInFolder(dir, PreludeKind::Strong);
        if (!strong.empty()) refs.push_back({strong, PreludeKind::Strong});
    }

    // Weak: an ancestor's weak prelude cascades down unless a folder strictly
    // below it (and at or above `folder`) contains a prelude of its own — the
    // "wall". A weak prelude is never stopped from reaching its own folder.
    for (std::size_t i = 0; i < chain.size(); ++i) {
        const fs::path weak = preludeInFolder(chain[i], PreludeKind::Weak);
        if (weak.empty()) continue;
        bool walled = false;
        for (std::size_t j = i + 1; j < chain.size() && !walled; ++j) {
            walled = folderHasPrelude(chain[j]);
        }
        if (!walled) refs.push_back({weak, PreludeKind::Weak});
    }
    return refs;
}

}  // namespace cp3::modules
