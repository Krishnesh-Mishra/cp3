// =============================================================================
// src/cli/src/commands/build/runtime_snapshot.cpp - runtime source mirror
// (DEFINITIONS).
//
// OWNERSHIP: cli module (src/cli/**). Implements
// detail/core/runtime_snapshot.hpp.
//
// `cp3c build/run --show-runtime` materializes a readable copy of the
// runtime and stdlib sources under .cp3/build/_runtime/, laid out exactly
// like the repository. The mirror is a SHOW-ONLY aid:
//
//   - the build never compiles from it and never depends on it; the runtime
//     translation units are compiled from their real source location, and
//     the object files live in the matching .cp3/cache/_runtime/ tree;
//   - it is created only when it does not exist, and an existing mirror is
//     never re-copied, merged or rewritten;
//   - editing (or deleting) any file under _runtime/ changes nothing about
//     the produced binary; the file README.md in the mirror says so.
//
// `cp3c clean` removes the mirror with the rest of .cp3/build.
// =============================================================================
#include "detail/core/runtime_snapshot.hpp"

#include <cstdio>
#include <string>
#include <system_error>

#include "detail/core/util.hpp"

namespace cp3::cli {

namespace {

// Plain one-shot copy: the mirror is created in full or not at all (a
// failure deletes the partial tree so the next --show-runtime retries).
bool copyFile(const fs::path& from, const fs::path& to, bool verbose) {
    std::error_code ec;
    fs::create_directories(to.parent_path(), ec);
    if (ec) {
        std::fprintf(stderr, "cp3c: cannot create '%s' for the runtime mirror\n",
                     to.parent_path().string().c_str());
        return false;
    }
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        std::fprintf(stderr, "cp3c: cannot copy '%s' to '%s': %s\n",
                     from.string().c_str(), to.string().c_str(),
                     ec.message().c_str());
        return false;
    }
    if (verbose) {
        std::fprintf(stderr, "cp3c: runtime mirror: %s\n", to.string().c_str());
    }
    return true;
}

// Copies every regular file under `from` into `to`, preserving layout.
bool copyTree(const fs::path& from, const fs::path& to, bool verbose) {
    std::error_code ec;
    if (!fs::is_directory(from, ec)) return true;  // no tree, nothing to copy
    fs::recursive_directory_iterator it(from, ec);
    const fs::recursive_directory_iterator end;
    if (ec) return false;
    for (; it != end; it.increment(ec)) {
        if (ec) return false;
        if (!it->is_regular_file(ec)) continue;
        const fs::path rel = fs::relative(it->path(), from, ec);
        if (ec || rel.empty()) return false;
        if (!copyFile(it->path(), to / rel, verbose)) return false;
    }
    return true;
}

std::string readmeText() {
    return
        "# Runtime mirror (show only)\n"
        "\n"
        "This directory is a readable copy of the cp3 runtime and stdlib\n"
        "sources, laid out exactly like the repository, so you can see what\n"
        "the generated C++ calls into:\n"
        "\n"
        "    src/runtime/include/cp3/core/runtime.hpp\n"
        "    src/runtime/src/core/runtime.cpp\n"
        "    src/std/console/runtime.cpp\n"
        "\n"
        "**Editing these files changes nothing.** The build never compiles\n"
        "from this tree. To change the runtime, edit the real sources in the\n"
        "repository and rebuild. The runtime objects the build actually links\n"
        "live in `.cp3/cache/_runtime/` and are reused automatically.\n"
        "\n"
        "This mirror is created by `cp3c build --show-runtime` (or\n"
        "`cp3c run --show-runtime`) only when it does not already exist. It is\n"
        "never re-copied, merged or rewritten by later builds. Delete it or run\n"
        "`cp3c clean` and pass `--show-runtime` again to regenerate it from the\n"
        "current sources.\n";
}

}  // namespace

fs::path copyRuntimeSnapshot(const BuildLayout& layout, bool verbose) {
    const fs::path root = layout.buildDir / "_runtime";

    // Show-only tree: when it already exists it is left exactly as it is.
    // Only the README is ensured so the "edits do nothing" note is always
    // there, even for a mirror created by an older cp3c.
    std::error_code ec;
    if (fs::is_directory(root, ec)) {
        if (!fs::exists(root / "README.md", ec)) {
            writeTextFile(root / "README.md", readmeText(), nullptr);
        }
        return root;
    }

    const fs::path repo = repositoryRoot();
    if (repo.empty()) {
        std::fputs("cp3c: cannot locate the cp3 source tree to copy the\n"
                   "runtime mirror\n", stderr);
        return {};
    }

    fs::create_directories(root, ec);
    if (ec) {
        std::fprintf(stderr, "cp3c: cannot create the runtime mirror at '%s'\n",
                     root.string().c_str());
        return {};
    }

    const auto fail = [&root]() {
        // A partial mirror is worse than none: remove it so the next
        // --show-runtime run retries from scratch.
        std::error_code rmEc;
        fs::remove_all(root, rmEc);
        return fs::path{};
    };

    // Every include root the build uses, mirrored in repository-relative
    // layout: runtime headers, typed arrays, common headers, every stdlib
    // family's include tree and any vendored third-party roots.
    for (const fs::path& includeRoot : layout.includeDirs) {
        std::error_code relEc;
        const fs::path rel = fs::relative(includeRoot, repo, relEc);
        if (relEc || rel.empty() || rel.generic_string().find("..") == 0) {
            continue;  // outside the repository: nothing meaningful to mirror
        }
        if (!fs::is_directory(includeRoot, ec)) {
            std::fprintf(stderr, "cp3c: cannot read '%s' for the runtime mirror\n",
                         includeRoot.string().c_str());
            return fail();
        }
        if (!copyTree(includeRoot, root / rel, verbose)) {
            std::fprintf(stderr,
                         "cp3c: cannot copy '%s' into the runtime mirror\n",
                         includeRoot.string().c_str());
            return fail();
        }
    }

    // The runtime sources this build selected, in repository-relative
    // layout. The build itself compiles these from their real location;
    // this copy exists only so the tree reads side by side with the
    // generated C++.
    for (const fs::path& src : layout.runtimeSources) {
        std::error_code relEc;
        const fs::path rel = fs::relative(src, repo, relEc);
        if (relEc || rel.empty() || rel.generic_string().find("..") == 0) {
            continue;
        }
        if (!copyFile(src, root / rel, verbose)) {
            return fail();
        }
    }

    if (!writeTextFile(root / "README.md", readmeText(), nullptr)) {
        std::fputs("cp3c: cannot write the runtime mirror README.md\n", stderr);
        return fail();
    }
    return root;
}

}  // namespace cp3::cli
