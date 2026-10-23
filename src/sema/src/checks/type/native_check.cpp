// =============================================================================
// src/sema/src/checks/native_check.cpp - the native interop surface (spec 19).
//
// `import native "id" as alias` binds a native binding's declarations under
// the alias: `alias.f(...)` is a call into the C++ library, and it is
// type-checked here against the parameter list the binding recorded - not
// against a C++ template wall.
//
// The binding models arrive with the compile options (the module resolver
// loaded them from `.cp3/native/<id>/native.json`); this unit owns the
// cp3-native policy: which member is a function, what its cp3 type is, and
// what a wrong argument says.
// =============================================================================
#include "detail/analyzer.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "cp3/diag/diagnostic_codes.hpp"
#include "cp3/host/native_bindings.hpp"

namespace cp3 {

namespace {

// The cp3 type of a native function: its parameters and its return type.
Type nativeFunctionType(const NativeFunction& fn) {
    std::vector<Type> params;
    params.reserve(fn.params.size());
    for (const NativeParam& param : fn.params) params.push_back(param.type);
    return Type::function(params, fn.returnType);
}

// The hint a wrong argument carries: what the C++ parameter really is.
std::string nativeConversionHint(const NativeParam& param) {
    switch (param.kind) {
        case NativeParamKind::CStr:
            return " (the C++ parameter is a `const char*`)";
        case NativeParamKind::ByteBuffer:
            return " (the C++ parameter is a byte pointer plus a length)";
        case NativeParamKind::NumberArray:
            return " (the C++ parameter is a number pointer plus a length)";
        default:
            return "";
    }
}

}  // namespace

// The binding a native alias names, or nullptr.
const NativeModule* Analyzer::nativeModuleFor(const std::string& alias) const {
    const auto it = nativeAliases_.find(alias);
    return it == nativeAliases_.end() ? nullptr : it->second;
}

// The alias -> binding map, built before the statements are checked so a call
// may appear before its import. The alias itself is declared ambient here:
// the module resolver already reported a missing or broken binding, so the
// name must still resolve (no second, cascading error).
void Analyzer::collectNativeAliases(const Program& program) {
    for (const StmtPtr& stmt : program.body) {
        if (!stmt || stmt->kind != NodeKind::ImportDeclaration) continue;
        const auto& imp = static_cast<const ImportDeclaration&>(*stmt);
        if (imp.kind != ImportKind::Native) continue;
        const std::string alias = imp.alias.empty() ? imp.source : imp.alias;
        if (alias.empty()) continue;
        declareAmbient(alias, imp.span, imp.nodeId);
        for (const NativeBinding& binding : options_.nativeBindings) {
            if (binding.id != imp.source) continue;
            nativeAliases_[alias] = &binding.module;
            registerNativeEnums(binding.module);
        }
    }
}

// The binding's enums join the enum table, so `dm.Color.Red` resolves to the
// enum type exactly like a cp3 `enum` declaration's member does.
void Analyzer::registerNativeEnums(const NativeModule& module) {
    for (const NativeEnum& en : module.enums) {
        EnumInfo info;
        info.name = nativeEnumType(module.id, en.cppQualified).name;
        for (const NativeEnumMember& member : en.members) {
            info.members.push_back(EnumMemberInfo{member.name, false});
        }
        enums_.emplace(info.name, std::move(info));
    }
}

// The cp3 type of one member of a binding: a function, a class or an enum.
// Type::error() means "not a native member": the receiver is not a native
// alias, so the caller keeps its own lowering.
Type Analyzer::nativeMemberType(MemberExpr& m) {
    if (!m.object || m.object->kind != NodeKind::Identifier) return Type::error();
    const auto& id = static_cast<const Identifier&>(*m.object);
    const NativeModule* module = nativeModuleFor(id.name);
    if (module == nullptr) return Type::error();
    return nativeMemberTypeOf(*module, m.property, m.span);
}

// The cp3 type of one member of a binding.
Type Analyzer::nativeMemberTypeOf(const NativeModule& module,
                                  const std::string& name, const SourceSpan& span) {
    if (const NativeFunction* fn = findNativeFunction(module, name)) {
        return nativeFunctionType(*fn);
    }
    if (const NativeBoundClass* cls = findNativeClass(module, name)) {
        return nativeClassType(module.id, cls->cppQualified);
    }
    if (const NativeEnum* en = findNativeEnum(module, name)) {
        return nativeEnumType(module.id, en->cppQualified);
    }
    report(diagcode::kNativeUnknownMember, span,
           "the native binding '" + module.id + "' has no declaration named '" +
               name + "'");
    return Type::unknown();
}

// The argument check of a native call: the recorded parameter type, plus the
// conversion the binding records for it (cstr / byteBuffer / numberArray).
void Analyzer::checkNativeArgument(const NativeFunction& fn,
                                   const NativeArgSite& site) {
    const NativeParam& param = *site.param;
    if (wouldAccept(site.type, param.type, false)) return;
    report(diagcode::kNativeArgumentType, site.span,
           "native call '" + fn.name + "': argument " +
               std::to_string(site.position) + " has type '" +
               pretty(site.type) + "', but parameter '" + param.name +
               "' expects '" + pretty(param.type) + "'" +
               nativeConversionHint(param));
}

// The whole call: the arity, then every argument.
void Analyzer::checkNativeCall(const CallExpr& call, const NativeFunction& fn) {
    const std::size_t given = call.arguments.size();
    if (given != fn.params.size()) {
        report(diagcode::kArityMismatch, call.span,
               "arity mismatch: native function '" + fn.name + "' expects " +
                   std::to_string(fn.params.size()) + " argument" +
                   (fn.params.size() == 1 ? "" : "s") + ", got " +
                   std::to_string(given));
    }
    for (std::size_t i = 0; i < call.arguments.size(); ++i) {
        Expr* arg = call.arguments[i].get();
        if (!arg) continue;
        if (arg->kind == NodeKind::SpreadElement) {
            // The C++ arity of a native call is fixed; a spread argument
            // cannot be checked against one parameter.
            inferArg(arg);
            report(diagcode::kArityMismatch, arg->span,
                   "a native call cannot take a spread argument: '" + fn.name +
                       "' has a fixed parameter list");
            continue;
        }
        const Type at = inferExpr(*arg);
        if (i >= fn.params.size()) continue;
        const NativeArgSite site{&fn.params[i], at, i + 1, arg->span};
        checkNativeArgument(fn, site);
    }
}

// The member-call hook: `alias.f(...)` is a call into the binding.
bool Analyzer::tryInferNativeCall(CallExpr& call, MemberExpr& m, Type& out) {
    if (!m.object || m.object->kind != NodeKind::Identifier) return false;
    const auto& id = static_cast<const Identifier&>(*m.object);
    const NativeModule* module = nativeModuleFor(id.name);
    if (module == nullptr) return false;
    const NativeFunction* fn = findNativeFunction(*module, m.property);
    if (fn == nullptr) {
        // A class or an enum is not callable; anything else is a miss the
        // member read already reported (inferMember).
        if (findNativeClass(*module, m.property) != nullptr ||
            findNativeEnum(*module, m.property) != nullptr) {
            report(diagcode::kNotCallable, call.span,
                   "'" + id.name + "." + m.property +
                       "' is a native type, not a callable function");
        }
        inferArgs(call);
        out = withType(call, Type::error());
        return true;
    }
    checkNativeCall(call, *fn);
    out = withType(call, fn->returnType);
    return true;
}

}  // namespace cp3
