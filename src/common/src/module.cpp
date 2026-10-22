// =============================================================================
// cp3/module.cpp - CompilationUnit ownership + the module graph.
//
// The graph is intentionally small: ModuleGraph owns every unit and the import
// edges, and compilationOrder() produces a depth-first post-order that mirrors
// ES-module evaluation order (docs/language-spec/18-modules-imports.md).
// =============================================================================
#include "cp3/module.hpp"

#include <cstdint>
#include <string>
#include <utility>

namespace cp3 {

const char* moduleResolutionName(ModuleResolution r) noexcept {
    switch (r) {
        case ModuleResolution::Local:   return "Local";
        case ModuleResolution::Std:     return "Std";
        case ModuleResolution::Package: return "Package";
        case ModuleResolution::Native:  return "Native";
        case ModuleResolution::Cpp:     return "Cpp";
        case ModuleResolution::Unknown: return "Unknown";
    }
    return "Unknown";
}

const char* preludeKindName(PreludeKind k) noexcept {
    switch (k) {
        case PreludeKind::None:   return "None";
        case PreludeKind::Same:   return "Same";
        case PreludeKind::Strong: return "Strong";
        case PreludeKind::Weak:   return "Weak";
    }
    return "None";
}

CompilationUnit* ModuleGraph::addUnit(const SourceFile* file) {
    auto unit = std::make_unique<CompilationUnit>(file);
    unit->id = static_cast<std::uint32_t>(storage_.size());
    if (file != nullptr && unit->moduleName.empty()) {
        unit->moduleName = file->canonicalPath;
    }
    CompilationUnit* raw = unit.get();
    storage_.push_back(std::move(unit));
    unitsView_.push_back(raw);
    return raw;
}

CompilationUnit* ModuleGraph::unit(std::uint32_t id) const {
    if (id >= unitsView_.size()) {
        return nullptr;
    }
    return unitsView_[id];
}

void ModuleGraph::addDependency(ModuleDependency dep) {
    deps_.push_back(std::move(dep));
}

void ModuleGraph::addNativeBinding(NativeBinding binding) {
    // One binding per id: a second import of the same id is the same binding.
    for (NativeBinding& existing : nativeBindings_) {
        if (existing.id == binding.id) {
            existing = std::move(binding);
            return;
        }
    }
    nativeBindings_.push_back(std::move(binding));
}

const NativeBinding* ModuleGraph::findNativeBinding(const std::string& id) const {
    for (const NativeBinding& binding : nativeBindings_) {
        if (binding.id == id) return &binding;
    }
    return nullptr;
}

std::vector<CompilationUnit*> ModuleGraph::compilationOrder() const {
    std::vector<CompilationUnit*> order;
    if (entry_ == nullptr) {
        return order;
    }

    std::vector<bool> visited(storage_.size(), false);

    // Iterative DFS post-order so deep import chains cannot overflow the stack.
    // Each frame tracks which outgoing edge we are about to explore.
    struct Frame {
        CompilationUnit* node;
        std::size_t nextEdge;
    };
    std::vector<Frame> stack;
    stack.push_back({entry_, 0});
    visited[entry_->id] = true;

    while (!stack.empty()) {
        Frame& frame = stack.back();
        std::size_t edge = frame.nextEdge;

        // Find the next resolved edge out of this node.
        CompilationUnit* next = nullptr;
        while (edge < deps_.size()) {
            const ModuleDependency& dep = deps_[edge];
            ++edge;
            if (dep.importer != frame.node->id) {
                continue;
            }
            if (!dep.resolved || dep.imported == UINT32_MAX) {
                continue;
            }
            CompilationUnit* imported = unit(dep.imported);
            if (imported == nullptr || visited[imported->id]) {
                continue;  // already scheduled - cycle tolerated
            }
            next = imported;
            break;
        }
        frame.nextEdge = edge;

        if (next != nullptr) {
            visited[next->id] = true;
            stack.push_back({next, 0});
            continue;
        }

        // No further unvisited dependencies: emit this unit (post-order).
        order.push_back(frame.node);
        stack.pop_back();
    }

    return order;
}

}  // namespace cp3
