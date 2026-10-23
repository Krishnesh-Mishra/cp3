// =============================================================================
// src/codegen/src/detail/worker.h - private worker-handle helpers (D80).
//
// The main side's `const w = new Worker(...)` is a handle value, not a value
// type: it has no representation in `classify` (a handle cannot be copied into
// a record field or formatted), but codegen still needs to track it through
// bindings and the `w.*` member surface. Codegen models that as a nominal
// `Class` marker:
//
//   $workerHandle  parts = {In, Out, <ownerNS>}   the main side (Worker<In, Out>)
//   $threadHandle  parts = {In, Out, <ownerNS>}   the worker side (Thread<...>)
//
// `Thread<In, Out>` / `Worker<In, Out>` annotations resolve through sema to
// `Class "Thread"` / `Class "Worker"` with the same parts, so both spellings are
// recognised here. The owner namespace (a `$ns:<namespace>` Class part) is what
// lets the main side spell a payload enum that the worker unit declares; an
// explicit annotation has no namespace part and falls back to the unit's own.
// =============================================================================
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "cp3/core/analysis.hpp"
#include "cp3/core/ast.hpp"
#include "cp3/codegen.hpp"
#include "cp3/diag/diagnostic.hpp"
#include "cp3/core/types.hpp"

namespace cp3::detail {

inline constexpr const char* kWorkerHandleLabel = "$workerHandle";
inline constexpr const char* kThreadHandleLabel = "$threadHandle";
inline constexpr const char* kChannelNamespacePrefix = "$ns:";

// The marker type for a `new Worker(...)` binding (parts: In, Out, ownerNS).
Type channelHandleType(bool workerSide, const Type& in, const Type& out,
                       const std::string& ownerNS);

// True when `t` is the main-side / worker-side handle (marker or annotation).
bool isWorkerHandleType(const Type& t);
bool isThreadHandleType(const Type& t);
bool isChannelHandleType(const Type& t);

// Reads the `{In, Out}` parts of a handle-shaped type; false when `t` is not a
// handle or does not carry two direction types.
bool channelHandleParts(const Type& t, Type& in, Type& out);

// The namespace that declares the handle's payload types ("" when the handle
// came from an explicit `Worker<In, Out>` annotation).
std::string channelOwnerNS(const Type& t);

// Reads a worker entry's `Thread<In, Out>` annotation through the sema type-node
// side table (the single source of truth, D80). Nullopt when the arrow has no
// single annotated parameter - the callback reports CP3905 in that case.
std::optional<WorkerChannel> channelOfArrow(const ArrowFunction& arrow,
                                           const SemaResult& sema);

// The worker unit's entry: its (first) default-export arrow, or nullptr.
inline const ArrowFunction* defaultExportArrow(const Program& program) {
    for (const auto& stmtPtr : program.body) {
        if (!stmtPtr || stmtPtr->kind != NodeKind::ExportDeclaration) continue;
        const auto& ed = static_cast<const ExportDeclaration&>(*stmtPtr);
        if (ed.kind != ExportKind::Default || !ed.defaultExpr) continue;
        if (ed.defaultExpr->kind != NodeKind::ArrowFunction) return nullptr;
        return static_cast<const ArrowFunction*>(ed.defaultExpr.get());
    }
    return nullptr;
}

// The parameter name of an entry arrow as written in source ("" when absent).
// `declare` keys by the cp3 name; the C++ spelling maps it at the use site.
inline std::string entryParamName(const ArrowFunction& arrow) {
    if (arrow.params.size() != 1 || !arrow.params[0]) return std::string();
    const Parameter& p = *arrow.params[0];
    if (!p.name || p.name->kind != NodeKind::Identifier) return std::string();
    return static_cast<const Identifier&>(*p.name).name;
}

// The worker unit's resolved entry: the default-export arrow, the channel its
// `Thread<In, Out>` annotation declares, and the C++ spellings of the two
// channel directions.
struct WorkerUnitEntry {
    const ArrowFunction* arrow = nullptr;
    std::string threadName;
    WorkerChannel channel;
    std::string inCpp;
    std::string outCpp;
};

// Which send surface a `postMessage`/`request` call lowers to: the main side's
// `postMessage`, the main side's `request`, or the worker side's
// `postMessage`. The void-direction wording and the runtime method differ.
enum class WorkerSendSurface { MainPost, MainRequest, WorkerPost };

// One inline `new Worker((thread: Thread<In, Out>) => ...)` site the entry-unit
// pre-scan resolved (emit/worker_scan.cpp).
struct InlineWorkerSite {
    WorkerSite site;
    const ArrowFunction* arrow = nullptr;
};

// Walks the entry unit for the inline form, validates each arrow (annotation,
// listener-free scope, no enclosing locals), reports CP3905/CP3909/CP3002, and
// appends the valid sites.
void collectInlineWorkerSites(const Program& program, const SemaResult& sema,
                              const std::unordered_set<std::string>& functions,
                              DiagnosticSink& sink,
                              std::vector<InlineWorkerSite>& out);

}  // namespace cp3::detail
