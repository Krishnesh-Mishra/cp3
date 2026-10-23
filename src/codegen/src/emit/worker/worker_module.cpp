// =============================================================================
// src/codegen/src/emit/worker_module.cpp - the worker UNIT's lowering (D80).
//
// A worker unit is a full module: it may declare functions, classes, enums
// and module bindings, and it may run statements of its own. The unit lowers
// to one translation unit whose entry point is
//
//   void cp3_default(::cp3::runtime::ThreadHandle<In, Out>& thread)
//
// - the default export is that entry (spec §13): it runs once, at worker
// start-up, and receives the worker-side handle. The unit's module bindings
// are `thread_local` state, initialized in the entry's prologue, so every
// worker holds its own copy (D80). The unit's own statements run in the same
// prologue, before the entry body - exactly as an entry unit's top-level
// statements run in `main`.
//
// The handle surface (the main side's `w.*` and the worker side's
// `thread.*`) lives in emit/worker_handles.cpp; the inline-worker pre-scan
// in emit/worker_scan.cpp.
// =============================================================================
#include "detail/generator/generator.h"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>

#include "cp3/diag/diagnostic_codes.hpp"
#include "detail/class/class_model.h"
#include "detail/generator/cpp_writer.h"
#include "detail/generator/mangle.h"
#include "detail/expr/pattern_emitter.h"
#include "detail/generator/type_map.h"
#include "detail/worker/worker.h"

namespace cp3 {

using namespace detail;

namespace {

// A printable label for a type in a diagnostic message: the nominal label
// when the type carries one (a class, an enum), the kind name otherwise.
std::string workerTypeLabel(const Type& t) {
    return t.name.empty() ? typeKindName(t.kind) : t.name;
}

}  // namespace

// -----------------------------------------------------------------------------
// The unit's entry
// -----------------------------------------------------------------------------

// Resolves the unit's entry arrow and the channel its `Thread<In, Out>`
// annotation declares. Reports the problem and returns false when the unit
// cannot be lowered.
bool Generator::resolveWorkerUnitEntry(const Program& program,
                                        detail::WorkerUnitEntry& out) {
    const ArrowFunction* entry = detail::defaultExportArrow(program);
    if (entry == nullptr) {
        notImplemented(program, "a worker module without a default-export arrow");
        return false;
    }
    const std::optional<WorkerChannel> channel =
        detail::channelOfArrow(*entry, sema_);
    if (!channel) {
        reportWorkerCode(*entry, diagcode::kWorkerMissingAnnotation,
                         "a worker entry must annotate its parameter as "
                         "`thread: Thread<In, Out>`; the annotation is the "
                         "single source of truth for the worker's channel");
        return false;
    }
    // The entry runs once at worker start-up: it is not a coroutine, it
    // carries no type parameters (the channel's types come from the
    // `Thread<In, Out>` annotation), and its parameter is the handle itself,
    // so it is a plain name.
    if (entry->isAsync) {
        reportWorkerCode(*entry, diagcode::kWorkerEntryShape,
                         "a worker entry cannot be async: it runs once at worker "
                         "start-up and its body is the worker thread's setup");
        return false;
    }
    if (!entry->typeParams.empty()) {
        reportWorkerCode(*entry, diagcode::kWorkerEntryShape,
                         "a worker entry cannot be generic: the worker's channel "
                         "types come from its `Thread<In, Out>` annotation, so "
                         "there is nothing to infer a type parameter from");
        return false;
    }
    const std::string threadName = detail::entryParamName(*entry);
    if (threadName.empty()) {
        reportWorkerCode(*entry, diagcode::kWorkerEntryShape,
                         "a worker entry's parameter must be a plain name: it is "
                         "the worker-side handle (`thread: Thread<In, Out>`)");
        return false;
    }
    // The channel's payload types may be enums the unit declares; their C++
    // spelling depends on the unit's own enum table.
    WorkerChannel resolved = *channel;
    rewriteEnumTypes(resolved.in);
    rewriteEnumTypes(resolved.out);
    const std::string inCpp = workerPayloadText(resolved.in, namesp_);
    const std::string outCpp = workerPayloadText(resolved.out, namesp_);
    if (inCpp.empty() || outCpp.empty()) {
        reportWorkerCode(*entry, diagcode::kWorkerPayloadNotCopyable,
                         "worker channel payloads must be copyable value types "
                         "(numbers, strings, booleans, arrays, records, enums, "
                         "or unions/options of those)");
        return false;
    }
    out.arrow = entry;
    out.threadName = threadName;
    out.channel = resolved;
    out.inCpp = inCpp;
    out.outCpp = outCpp;
    return true;
}

// -----------------------------------------------------------------------------
// The unit's body
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
        case NodeKind::ClassDeclaration:
            // A class in a worker unit (spec 15): the model is collected
            // before the walk and its struct is emitted with the unit's other
            // declarations.
            emitTopLevelClass(static_cast<const ClassDeclaration&>(s));
            return;
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
            // An ordinary statement: it runs when the worker thread starts,
            // before the entry arrow (the worker unit's prologue), exactly as
            // an entry unit's top-level statements run in `main`.
            stmt(s, prologue, 1);
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
            // A destructuring binding: the source is materialized and every
            // name the pattern binds is read out of it. The names are the
            // worker's module state (`thread_local` globals), because the
            // entry body returns before the message handlers run - a local
            // would dangle.
            if (!d.init) {
                reportTypeError(d, "a destructuring binding needs an "
                                   "initializer");
                continue;
            }
            const std::optional<Type> srcType = resolvedBindingType(d);
            std::string initText;
            {
                const std::optional<Type> savedExpected = expected_;
                expected_ = srcType;
                initText = expr(*d.init);
                expected_ = savedExpected;
            }
            const std::string tmp =
                "cp3_d" + std::to_string(uniqueCounter_++);
            line(prologue, 1, "auto " + tmp + " = " + initText + ";");
            PatternEmitter emitter(*this);
            emitter.globals = &defs;
            emitter.emitBindings(*d.id, tmp, srcType, prologue, 1);
            continue;
        }
        const CppInfo ci = bindingCpp(d);
        if (!ci.representable || ci.shape == Shape::Void) {
            // A worker module binding lives in the worker thread's own state
            // (a `thread_local`), so it must have a value spelling: a live
            // handle, a promise or a function has none.
            reportWorkerCode(d, diagcode::kWorkerModuleForm,
                             "this worker module binding has no value spelling "
                             "in a worker unit (its type is '" +
                                 workerTypeLabel(ci.type) + "')");
            continue;
        }
        const std::string raw = static_cast<const Identifier&>(*d.id).name;
        const std::string name = mapIdent(raw);
        defs += "thread_local " + ci.cpp + " " + name + "{};\n";
        if (d.init) line(prologue, 1, name + " = " + expr(*d.init) + ";");
        declare(raw, resolvedBindingType(d));
    }
}

// The unit's own body: its declarations and its statements, in source order.
// The module bindings' initializers and the unit's statements both land in
// the prologue, so they run in source order when the worker thread starts.
void Generator::emitWorkerUnitBody(const Program& program, std::string& decls,
                                    std::string& defs, std::string& prologue) {
    for (const auto& stmtPtr : program.body) {
        if (!stmtPtr) continue;
        collectDeclaredNames(*stmtPtr);
        if (stmtPtr->kind == NodeKind::ImportDeclaration) {
            const auto& imp = static_cast<const ImportDeclaration&>(*stmtPtr);
            if (imp.source == "std/console") consoleImported_ = true;
        }
    }
    {
        // The unit's statements are not a function body, so a `return` there
        // has no value.
        const std::string savedReturn = currentReturn_;
        currentReturn_ = "void";
        for (const auto& stmtPtr : program.body) {
            if (stmtPtr) emitWorkerTopLevel(*stmtPtr, decls, defs, prologue);
        }
        currentReturn_ = savedReturn;
    }
    if (!topLevelAwait_) return;
    // The unit's statements await: they run inside a coroutine the worker
    // thread's own loop drives (D71: one loop per thread, and the worker
    // thread runs the entry before its loop starts).
    needsEventLoop_ = true;
    std::string body = prologue;
    prologue.clear();
    prologue += "auto cp3_top_level = [&]() -> ::cp3::runtime::Promise<void> {\n";
    prologue += indentText(body, "    ");
    prologue += "    co_return;\n";
    prologue += "};\n";
    prologue += "cp3_top_level();\n";
}

// -----------------------------------------------------------------------------
// The unit's translation unit
// -----------------------------------------------------------------------------

GeneratedModule Generator::runWorkerModule(const Program& program) {
    workerModule_ = true;
    needsWorker_ = true;
    // Resolve the unit's types before the entry signature: a channel payload
    // may be an enum (locally declared or imported from a local module), and
    // its C++ spelling depends on `enums_`.
    collectJsonShapes(program);
    collectEnums(program);
    // The spec 15 object surface: a worker unit is a full module, so its
    // classes and interface shapes are registered up front - a module binding
    // of a class type needs the class's C++ spelling.
    detail::ModelRegistry::get().reset();
    detail::ModelRegistry::get().setNamespace(namesp_);
    collectModels(program);
    detail::WorkerUnitEntry entry;
    if (!resolveWorkerUnitEntry(program, entry)) return GeneratedModule{};
    std::string decls;
    std::string defs;
    std::string prologue;
    emitWorkerUnitBody(program, decls, defs, prologue);
    // The worker TU is a full module: its own enums must precede the entry
    // signature that spells a channel payload enum, and its classes must be
    // complete before anything names them.
    emitAllClasses();
    objectDecls_ = objectStructsText();
    if (!objectDecls_.empty()) needsRef_ = true;
    decls = enumDecls_ + classFwdDecls_ + jsonShapeDecls_ + objectDecls_ +
            classDecls_ + decls;
    defs = classDefs_ + defs;
    const std::string signature =
        "void cp3_default(::cp3::runtime::ThreadHandle<" + entry.inCpp + ", " +
        entry.outCpp + ">& " + mapIdent(entry.threadName) + ")";
    line(decls, 0, signature + ";");
    defs += signature + " {\n" + prologue;
    std::string body;
    emitWorkerEntryBody(*entry.arrow, entry.channel.in, entry.channel.out, body);
    defs += body;
    defs += "}\n";
    const std::string globals =
        objectGlobalDeclsFor(decls + defs + entry.inCpp + entry.outCpp, true);
    applyUsageIncludes(decls + defs + globals + entry.inCpp + entry.outCpp);
    GeneratedModule module;
    module.header = buildHeader(decls, globals);
    module.source = buildSource(decls, defs, globals, std::string());
    return module;
}

}  // namespace cp3
