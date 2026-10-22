// =============================================================================
// src/modules/src/support/native_import.cpp - the `native` import kind.
//
// OWNERSHIP: modules module (src/modules/**). Implements the native_import
// concern declared in detail/loader.h.
//
// `import native "id" as alias` resolves to
// `<project>/.cp3/native/<id>/native.json` (the metadata `cp3c
// native-install` wrote) and to its sibling `index.cp3`. The binding is read
// and validated here, so a missing, malformed or stale binding is reported at
// the import site - not as an opaque C++ error three stages later.
//
// The loaded model travels to sema and codegen through the module graph
// (cp3/module.hpp), which is the only channel between the stages.
// =============================================================================
#include "detail/loader.h"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "cp3/diagnostic_codes.hpp"
#include "cp3/native_bindings.hpp"

#include "detail/file_paths.h"

namespace cp3::modules {

namespace fs = std::filesystem;

namespace {

// The recorded paths of a binding are absolute, or relative to the project
// root (the directory that holds `.cp3/native/<id>`). A relative path is
// resolved once, here, so every later stage sees the same absolute path.
void resolveRecordedPath(std::string& path, const fs::path& projectRoot) {
    if (path.empty()) return;
    const fs::path recorded(path);
    if (recorded.is_absolute()) return;
    path = (projectRoot / recorded).generic_string();
}

// A binding id is a plain name, never a path: the binding lives in
// `<project>/.cp3/native/<id>`, and an id with a separator in it would read
// outside that folder.
bool isPlainBindingId(const std::string& id) {
    if (id.empty() || id == "." || id == "..") return false;
    for (const char c : id) {
        if (c == '/' || c == '\\' || c == ':') return false;
    }
    return true;
}

}  // namespace

void ProgramLoader::resolveNativePaths(NativeModule& module,
                                        const fs::path& projectRoot) {
    for (std::string& dir : module.includeDirs) {
        resolveRecordedPath(dir, projectRoot);
    }
    for (std::string& dir : module.libDirs) {
        resolveRecordedPath(dir, projectRoot);
    }
}

// The diagnostic a failed binding load reports, and its remedy.
void ProgramLoader::reportNativeFailure(const std::string& id,
                                         const fs::path& dir,
                                         const NativeReadResult& result,
                                         const SourceSpan& span) {
    const char* code = diagcode::kNativeBindingInvalid;
    std::string message = result.message;
    std::string remedy = result.remedy;
    std::vector<std::string> suggestions;
    switch (result.status) {
        case NativeReadStatus::Missing:
            code = diagcode::kNativeBindingNotFound;
            message = "no native binding '" + id + "' at '" +
                      dir.generic_string() + "'";
            remedy = "run 'cp3 native-install <sdk> --name " + id +
                     "' to create it";
            break;
        case NativeReadStatus::Stale:
            code = diagcode::kNativeBindingStale;
            break;
        default:
            code = diagcode::kNativeBindingInvalid;
            break;
    }
    // The remedy is part of the message: the compact renderer does not print
    // the `help:` lines, and the remedy is the answer to the question.
    if (!remedy.empty()) {
        message += "; " + remedy;
        suggestions.push_back(remedy);
    }
    reportOnce(code, span, message, suggestions);
}

// Resolves one `import native "id" as alias` statement. Returns true when the
// binding was loaded and parked in the graph.
bool ProgramLoader::loadNativeBinding(const ImportDeclaration& decl) {
    const std::string& id = decl.source;
    if (!isPlainBindingId(id)) {
        reportOnce(diagcode::kNativeBindingNotFound, decl.span,
                   "a native import needs a binding id: "
                   "import native \"<id>\" as <alias>");
        return false;
    }
    const fs::path dir = nativeRoot_ / fs::path(id);
    const NativeReadResult result = readNativeBinding(dir);
    if (result.status != NativeReadStatus::Ok) {
        reportNativeFailure(id, dir, result, decl.span);
        return false;
    }
    NativeBinding binding;
    binding.id = id;
    binding.alias = decl.alias.empty() ? id : decl.alias;
    binding.dir = dir.generic_string();
    binding.module = result.module;
    resolveNativePaths(binding.module, projectRoot_);
    graph_->addNativeBinding(std::move(binding));
    return true;
}

}  // namespace cp3::modules
