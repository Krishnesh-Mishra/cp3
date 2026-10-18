// =============================================================================
// modules/tests/prelude_scan_test.cpp - §18 discovery + D4 classification.
//
// OWNERSHIP: modules module (src/modules/tests/**). Opt-in driver:
//   cmake -B build/scratch/modules -S . -DCP3_MODULES_TESTS=ON
//   cmake --build build/scratch/modules --target cp3_modules_prelude_scan_test
//
// Builds a small prelude tree on disk, then checks discovery order, the weak
// "wall", specifier classification and the std registry. Returns 0 on success.
// =============================================================================
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "export_surface.h"
#include "file_paths.h"
#include "prelude_scan.h"
#include "specifier.h"
#include "std_registry.h"

namespace fs = std::filesystem;
using namespace cp3;
using namespace cp3::modules;

namespace {

int gFailures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++gFailures;
    }
}

void write(const fs::path& path, const char* text = "") {
    std::ofstream out(path, std::ios::binary);
    out << text;
}

std::vector<std::string> kindsOf(const std::vector<PreludeRef>& refs) {
    std::vector<std::string> kinds;
    for (const PreludeRef& ref : refs) kinds.emplace_back(preludeKindName(ref.kind));
    return kinds;
}

bool kindsAre(const std::vector<PreludeRef>& refs, const std::vector<std::string>& want) {
    return kindsOf(refs) == want;
}

void testDiscovery(const fs::path& root) {
    // strong + same at the root, weak at the root; a walled child with a
    // strong prelude of its own.
    fs::create_directories(root / "sub" / "walled");
    write(root / "_prelude.same.cp3");
    write(root / "_prelude.cp3");
    write(root / "_prelude.weak.cp3");
    write(root / "sub" / "x.cp3");
    write(root / "sub" / "walled" / "_prelude.cp3");
    write(root / "sub" / "walled" / "y.cp3");

    const std::vector<PreludeRef> atRoot = preludesForFolder(root);
    check(kindsAre(atRoot, {"Same", "Strong", "Weak"}), "root: same, strong, weak");

    const std::vector<PreludeRef> inSub = preludesForFolder(root / "sub");
    check(kindsAre(inSub, {"Strong", "Weak"}), "sub: strong + weak, no same");

    const std::vector<PreludeRef> walled = preludesForFolder(root / "sub" / "walled");
    check(kindsAre(walled, {"Strong", "Strong"}), "walled: two strong, weak stopped");
    check(folderHasPrelude(root / "sub" / "walled"), "wall folder has a prelude");
}

void testClassification() {
    check(classifyModuleSpecifier("./x.cp3") == ModuleResolution::Local, "./x.cp3 local");
    check(classifyModuleSpecifier("../x.cp3") == ModuleResolution::Local, "../x.cp3 local");
    check(classifyModuleSpecifier("worker.cp3") == ModuleResolution::Local, "bare .cp3 local");
    check(classifyModuleSpecifier("std/console") == ModuleResolution::Std, "std/ console");
    check(classifyModuleSpecifier("cpp/iostream") == ModuleResolution::Cpp, "cpp/ iostream");
    check(classifyModuleSpecifier("mathkit") == ModuleResolution::Package, "bare = package");
}

void testStdRegistry() {
    const ExportSurface* collections = stdModuleSurface("std/collections");
    check(collections != nullptr, "std/collections known");
    if (collections != nullptr) {
        check(collections->named.count("Map") == 1, "Map exported");
        check(collections->named.count("Set") == 1, "Set exported");
        check(collections->named.count("Mutex") == 0, "Mutex not exported");
    }
    const ExportSurface* console = stdModuleSurface("std/console");
    check(console != nullptr && console->hasDefault, "std/console default");
    check(stdModuleSurface("std/doesnotexist") == nullptr, "unknown std module");
}

}  // namespace

int main() {
    std::error_code ec;
    const fs::path root =
        fs::temp_directory_path(ec) / fs::path("cp3_modules_prelude_scan_test");
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    if (ec) {
        std::fprintf(stderr, "FAIL: cannot create %s\n", root.string().c_str());
        return 1;
    }

    testDiscovery(root);
    testClassification();
    testStdRegistry();

    fs::remove_all(root, ec);
    if (gFailures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", gFailures);
        return 1;
    }
    std::printf("cp3_modules prelude_scan_test: all checks passed\n");
    return 0;
}
