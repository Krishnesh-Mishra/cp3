// =============================================================================
// src/codegen/src/emit/worker.cpp - the worker-handle surface (D80, spec 13).
//
// Two halves of one pipeline:
//
//   * the ENTRY unit: `new Worker("./x.cp3")` lowers to
//     `::cp3::runtime::startWorker<In, Out>(&::<worker-ns>::cp3_default)`. The
//     CLI resolved the path and generated the worker unit's TU; `In`/`Out` come
//     from that unit's `Thread<In, Out>` annotation (the single source of
//     truth), handed over in `CodegenOptions::workers`. The inline form
//     `new Worker((thread: Thread<In, Out>) => { ... })` synthesizes the same
//     worker unit here: the arrow becomes `cp3_inline_worker_<n>` in this TU,
//     with its own module state and no enclosing locals (CP3909).
//   * the HANDLE surface (`emitWorkerHandleCall`): `w.postMessage`,
//     `w.on("message" | "error", l)`, `await w.request(v)`, `w.terminate()`,
//     and the worker side's `thread.on(...)` / `thread.postMessage(v)`. Payloads
//     are copyable value types (numbers, strings, booleans, arrays, `$o`
//     records, enums, unions/options of those); a void direction has no send
//     surface (CP3906).
//
// The worker unit itself (`runWorkerModule`) is the same lowering as the entry
// form: its default-export arrow becomes
// `void cp3_default(::cp3::runtime::ThreadHandle<In, Out>& thread)`, with the
// unit's functions, enums and imports visible and its module variables held as
// `thread_local` state initialized by the entry prologue.
// =============================================================================
#include "detail/generator.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cp3/diagnostic_codes.hpp"
#include "detail/cpp_writer.h"
#include "detail/mangle.h"
#include "detail/type_map.h"
#include "detail/worker.h"

namespace cp3 {

using namespace detail;

namespace {

// The worker unit's entry: its (first) default-export arrow, or nullptr.
const ArrowFunction* defaultExportArrow(const Program& program) {
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
std::string entryParamName(const ArrowFunction& arrow) {
    if (arrow.params.size() != 1 || !arrow.params[0]) return std::string();
    const Parameter& p = *arrow.params[0];
    if (!p.name || p.name->kind != NodeKind::Identifier) return std::string();
    return static_cast<const Identifier&>(*p.name).name;
}

}  // namespace

// -----------------------------------------------------------------------------
// The handle marker model (detail/worker.h)
// -----------------------------------------------------------------------------

namespace detail {

Type channelHandleType(bool workerSide, const Type& in, const Type& out,
                       const std::string& ownerNS) {
    Type t(TypeKind::Class, workerSide ? kThreadHandleLabel : kWorkerHandleLabel);
    t.parts.push_back(in);
    t.parts.push_back(out);
    if (!ownerNS.empty()) {
        Type ns(TypeKind::Class, std::string(kChannelNamespacePrefix) + ownerNS);
        t.parts.push_back(std::move(ns));
    }
    return t;
}

bool isWorkerHandleType(const Type& t) {
    return t.kind == TypeKind::Class &&
           (t.name == kWorkerHandleLabel || t.name == "Worker") &&
           t.parts.size() >= 2;
}

bool isThreadHandleType(const Type& t) {
    return t.kind == TypeKind::Class &&
           (t.name == kThreadHandleLabel || t.name == "Thread") &&
           t.parts.size() >= 2;
}

bool isChannelHandleType(const Type& t) {
    return isWorkerHandleType(t) || isThreadHandleType(t);
}

bool channelHandleParts(const Type& t, Type& in, Type& out) {
    if (!isChannelHandleType(t) || t.parts.size() < 2) return false;
    in = t.parts[0];
    out = t.parts[1];
    return true;
}

std::string channelOwnerNS(const Type& t) {
    if (t.parts.size() < 3) return std::string();
    const Type& ns = t.parts[2];
    const std::string prefix = kChannelNamespacePrefix;
    if (ns.kind != TypeKind::Class || ns.name.rfind(prefix, 0) != 0) {
        return std::string();
    }
    return ns.name.substr(prefix.size());
}

std::optional<WorkerChannel> channelOfArrow(const ArrowFunction& arrow,
                                            const SemaResult& sema) {
    if (arrow.params.size() != 1 || !arrow.params[0]) return std::nullopt;
    const Parameter& p = *arrow.params[0];
    if (p.isRest || !p.type) return std::nullopt;
    const Type* t = sema.typeOf(p.type->nodeId);
    if (t == nullptr || t->kind != TypeKind::Class || t->name != "Thread" ||
        t->parts.size() != 2) {
        return std::nullopt;
    }
    if (t->parts[0].isError() || t->parts[1].isError()) return std::nullopt;
    WorkerChannel channel;
    channel.in = t->parts[0];
    channel.out = t->parts[1];
    return channel;
}

}  // namespace detail

std::optional<WorkerChannel> resolveWorkerChannel(const Program& program,
                                                  const SemaResult& sema) {
    const ArrowFunction* arrow = defaultExportArrow(program);
    if (arrow == nullptr) return std::nullopt;
    return detail::channelOfArrow(*arrow, sema);
}

// -----------------------------------------------------------------------------
// The entry unit: `new Worker(...)`
// -----------------------------------------------------------------------------

const WorkerSite* Generator::workerSiteAt(std::uint32_t nodeId) const {
    const auto it = workerSites_.find(nodeId);
    return it == workerSites_.end() ? nullptr : &it->second;
}

std::size_t Generator::workerInlineIndex(std::uint32_t nodeId) const {
    for (std::size_t i = 0; i < inlineWorkerOrder_.size(); ++i) {
        if (inlineWorkerOrder_[i] == nodeId) return i;
    }
    return static_cast<std::size_t>(-1);
}

std::optional<Type> Generator::workerBindingFor(const NewExpr& n) const {
    if (!n.callee || n.callee->kind != NodeKind::Identifier) return std::nullopt;
    if (static_cast<const Identifier&>(*n.callee).name != "Worker") {
        return std::nullopt;
    }
    if (functions_.count("Worker") != 0) return std::nullopt;
    const WorkerSite* site = workerSiteAt(n.nodeId);
    if (site == nullptr) return std::nullopt;
    const std::string ownerNS = site->inlineSite
                                    ? namesp_
                                    : suggestNamespace(site->moduleName);
    return channelHandleType(false, site->in, site->out, ownerNS);
}

std::optional<Type> Generator::workerCallType(const CallExpr& c) const {
    if (!c.callee || c.callee->kind != NodeKind::MemberExpr) return std::nullopt;
    const auto& m = static_cast<const MemberExpr&>(*c.callee);
    if (!m.object) return std::nullopt;
    Type in;
    Type out;
    if (!channelHandleParts(effectiveType(*m.object), in, out)) return std::nullopt;
    if (m.property == "request") return promiseOf(out);
    return Type::voidType();
}

std::string Generator::emitWorkerNew(const NewExpr& n) {
    if (!n.callee || n.callee->kind != NodeKind::Identifier) return std::string();
    if (static_cast<const Identifier&>(*n.callee).name != "Worker" ||
        functions_.count("Worker") != 0) {
        return std::string();
    }
    if (!n.typeArgs.empty()) {
        reportWorkerCode(n, diagcode::kWorkerHandleMember,
                         "new Worker(...) does not take type arguments; the "
                         "worker entry's Thread<In, Out> annotation is the "
                         "single source of truth");
        return "0.0";
    }
    if (n.arguments.size() != 1 || !n.arguments.front()) {
        reportWorkerCode(n, diagcode::kWorkerComputedPath,
                         "new Worker(...) takes one static `.cp3` path or one "
                         "inline `(thread: Thread<In, Out>) => ...` arrow");
        return "0.0";
    }
    const WorkerSite* site = workerSiteAt(n.nodeId);
    if (site == nullptr) {
        // An inline arrow was already reported by the pre-scan.
        if (n.arguments.front()->kind != NodeKind::ArrowFunction) {
            reportWorkerCode(n, diagcode::kWorkerComputedPath,
                             "the worker file path must be a static string "
                             "literal naming a `.cp3` file (a computed path is "
                             "not a compile-time unit)");
        }
        return "0.0";
    }
    const std::string ownerNS =
        site->inlineSite ? namesp_ : suggestNamespace(site->moduleName);
    const std::string inCpp = workerPayloadText(site->in, ownerNS);
    const std::string outCpp = workerPayloadText(site->out, ownerNS);
    needsWorker_ = true;
    if (inCpp.empty() || outCpp.empty()) {
        reportWorkerCode(n, diagcode::kWorkerPayloadNotCopyable,
                         "worker channel payloads must be copyable value types "
                         "(numbers, strings, booleans, arrays, records, enums, "
                         "or unions/options of those)");
        return "0.0";
    }
    if (site->inlineSite) {
        const std::size_t index = workerInlineIndex(n.nodeId);
        if (index == static_cast<std::size_t>(-1)) return "0.0";
        return "::cp3::runtime::startWorker<" + inCpp + ", " + outCpp +
               ">(&cp3_inline_worker_" + std::to_string(index) + ")";
    }
    return "::cp3::runtime::startWorker<" + inCpp + ", " + outCpp +
           ">(&::" + ownerNS + "::cp3_default)";
}

// -----------------------------------------------------------------------------
// The inline form: synthesize one worker entry in this unit
// -----------------------------------------------------------------------------

void Generator::collectWorkerSites(const Program& program) {
    if (options_.workerModule) return;
    for (const WorkerSite& site : options_.workers) {
        if (site.nodeId != 0) workerSites_[site.nodeId] = site;
    }
    std::vector<InlineWorkerSite> inlineSites;
    collectInlineWorkerSites(program, sema_, functions_, sink_, inlineSites);
    for (InlineWorkerSite& found : inlineSites) {
        const std::uint32_t id = found.site.nodeId;
        workerSites_[id] = std::move(found.site);
        inlineWorkerArrows_[id] = found.arrow;
        inlineWorkerOrder_.push_back(id);
    }
    // The CLI resolved a worker unit's annotation before this unit's `enums_`
    // were known; a payload enum arrives as a nominal Class and becomes an
    // enum here (imported enums included). The worker TU performs the same
    // rewrite on its own channel.
    for (auto& entry : workerSites_) {
        rewriteEnumTypes(entry.second.in);
        rewriteEnumTypes(entry.second.out);
    }
}

void Generator::emitInlineWorkerEntries(std::string& decls, std::string& defs) {
    for (std::size_t i = 0; i < inlineWorkerOrder_.size(); ++i) {
        const std::uint32_t id = inlineWorkerOrder_[i];
        const WorkerSite* site = workerSiteAt(id);
        const auto arrowIt = inlineWorkerArrows_.find(id);
        if (site == nullptr || arrowIt == inlineWorkerArrows_.end() ||
            arrowIt->second == nullptr) {
            continue;
        }
        const std::string inCpp = workerPayloadText(site->in, namesp_);
        const std::string outCpp = workerPayloadText(site->out, namesp_);
        if (inCpp.empty() || outCpp.empty()) continue;  // reported at the site
        needsWorker_ = true;
        const std::string name = "cp3_inline_worker_" + std::to_string(i);
        const std::string signature = "void " + name +
                                      "(::cp3::runtime::ThreadHandle<" + inCpp +
                                      ", " + outCpp + ">& thread)";
        line(decls, 0, signature + ";");
        defs += signature + " {\n";
        std::string body;
        emitWorkerEntryBody(*arrowIt->second, site->in, site->out, body);
        defs += body;
        defs += "}\n\n";
    }
}

void Generator::emitWorkerEntryBody(const ArrowFunction& arrow, const Type& in,
                                    const Type& outType, std::string& outText) {
    ++nestingDepth_;
    pushScope();
    const std::string savedReturn = currentReturn_;
    const bool savedInAsync = inAsyncBody_;
    currentReturn_ = "void";
    inAsyncBody_ = false;
    const std::string threadName = entryParamName(arrow);
    if (!threadName.empty()) {
        declare(threadName, channelHandleType(true, in, outType, namesp_));
    }
    if (arrow.body && arrow.expressionBody) {
        line(outText, 1, expr(*static_cast<const Expr*>(arrow.body.get())) + ";");
    } else if (arrow.body) {
        stmt(*static_cast<const Stmt*>(arrow.body.get()), outText, 1);
    }
    inAsyncBody_ = savedInAsync;
    currentReturn_ = savedReturn;
    popScope();
    --nestingDepth_;
}

// -----------------------------------------------------------------------------
// The worker unit: full module -> `cp3_default(ThreadHandle<In, Out>&)`
// -----------------------------------------------------------------------------

void Generator::emitWorkerTopLevel(const Stmt& s, std::string& decls,
                                   std::string& defs, std::string& prologue) {
    switch (s.kind) {
        case NodeKind::ImportDeclaration:
            return;  // imports are compile-time; console was noted by the caller
        case NodeKind::FunctionDeclaration:
            addFunction(static_cast<const FunctionDeclaration&>(s), decls, defs);
            return;
        case NodeKind::EnumDeclaration:
            return;  // collectEnums ran before the walk
        case NodeKind::InterfaceDeclaration:
        case NodeKind::TypeAliasDeclaration:
            return;  // type-only: erased by sema
        case NodeKind::VariableDeclaration:
            emitWorkerModuleVar(static_cast<const VariableDeclaration&>(s), defs,
                                prologue);
            return;
        case NodeKind::ExportDeclaration: {
            const auto& ed = static_cast<const ExportDeclaration&>(s);
            if (ed.defaultExpr) return;  // the entry arrow
            if (ed.declaration) {
                emitWorkerTopLevel(*ed.declaration, decls, defs, prologue);
                return;
            }
            notImplemented(ed, "re-export in a worker module");
            return;
        }
        default:
            notImplemented(s, "this worker-module statement");
            return;
    }
}

void Generator::emitWorkerModuleVar(const VariableDeclaration& vd,
                                    std::string& defs,
                                    std::string& prologue) {
    for (const auto& declPtr : vd.declarations) {
        if (!declPtr) continue;
        const VariableDeclarator& d = *declPtr;
        if (!d.id || d.id->kind != NodeKind::Identifier) {
            notImplemented(d, "destructuring binding in a worker module");
            continue;
        }
        const CppInfo ci = bindingCpp(d);
        if (!ci.representable || ci.shape == Shape::Void) {
            notImplemented(d, "this worker module variable");
            continue;
        }
        const std::string raw = static_cast<const Identifier&>(*d.id).name;
        const std::string name = mapIdent(raw);
        defs += "thread_local " + ci.cpp + " " + name + "{};\n";
        if (d.init) line(prologue, 1, name + " = " + expr(*d.init) + ";");
        declare(raw, resolvedBindingType(d));
    }
}

GeneratedModule Generator::runWorkerModule(const Program& program) {
    workerModule_ = true;
    needsWorker_ = true;
    // Resolve the unit's types before the entry signature: a channel payload
    // may be an enum (locally declared or imported from a local module), and
    // its C++ spelling depends on `enums_`.
    collectJsonShapes(program);
    collectEnums(program);
    const ArrowFunction* entry = defaultExportArrow(program);
    const std::string threadName = entry ? entryParamName(*entry) : std::string();
    std::optional<WorkerChannel> channel =
        entry ? channelOfArrow(*entry, sema_) : std::nullopt;
    if (channel) {
        rewriteEnumTypes(channel->in);
        rewriteEnumTypes(channel->out);
    }
    if (entry == nullptr) {
        notImplemented(program, "a worker module without a default-export arrow");
        return GeneratedModule{};
    }
    if (!channel) {
        reportWorkerCode(*entry, diagcode::kWorkerMissingAnnotation,
                         "a worker entry must annotate its parameter as "
                         "`thread: Thread<In, Out>`; the annotation is the "
                         "single source of truth for the worker's channel");
        return GeneratedModule{};
    }
    if (entry->isAsync || !entry->typeParams.empty() || threadName.empty()) {
        notImplemented(*entry, "this worker entry arrow");
        return GeneratedModule{};
    }
    const std::string inCpp = workerPayloadText(channel->in, namesp_);
    const std::string outCpp = workerPayloadText(channel->out, namesp_);
    if (inCpp.empty() || outCpp.empty()) {
        reportWorkerCode(*entry, diagcode::kWorkerPayloadNotCopyable,
                         "worker channel payloads must be copyable value types "
                         "(numbers, strings, booleans, arrays, records, enums, "
                         "or unions/options of those)");
        return GeneratedModule{};
    }
    for (const auto& stmtPtr : program.body) {
        if (!stmtPtr) continue;
        collectFunctionNames(*stmtPtr);
        if (stmtPtr->kind == NodeKind::ImportDeclaration) {
            const auto& imp = static_cast<const ImportDeclaration&>(*stmtPtr);
            if (imp.source == "std/console") consoleImported_ = true;
        }
    }
    std::string decls;
    std::string defs;
    std::string prologue;
    for (const auto& stmtPtr : program.body) {
        if (stmtPtr) emitWorkerTopLevel(*stmtPtr, decls, defs, prologue);
    }
    // The worker TU is a full module: its own enums must precede the entry
    // signature that spells a channel payload enum.
    decls = enumDecls_ + decls;
    const std::string signature =
        "void cp3_default(::cp3::runtime::ThreadHandle<" + inCpp + ", " +
        outCpp + ">& " + mapIdent(threadName) + ")";
    line(decls, 0, signature + ";");
    defs += signature + " {\n" + prologue;
    std::string body;
    emitWorkerEntryBody(*entry, channel->in, channel->out, body);
    defs += body;
    defs += "}\n";
    const std::string globals =
        objectGlobalDeclsFor(decls + defs + inCpp + outCpp, true);
    noteEmittedSpellings(decls + defs + globals + inCpp + outCpp);
    GeneratedModule module;
    module.header = buildHeader(decls, globals);
    module.source = buildSource(decls, defs, globals, std::string());
    return module;
}

}  // namespace cp3
