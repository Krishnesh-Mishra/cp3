// =============================================================================
// src/codegen/src/emit/native_call.cpp - the native interop lowering.
//
// OWNERSHIP: codegen module. Lowers `alias.f(...)` (an `import native "id" as
// alias` call) into the real C++ call the binding recorded: the `cppName` of
// the function, one C++ argument per recorded parameter (a buffer or number
// array parameter expands to a pointer and a length, per the recorded
// `cppArgCount`), and the `returnsCStr` mapping of a `const char*` result.
//
// The binding's headers are included by the generated .cpp
// (support/module_writer.cpp); the include roots, defines and libraries come
// from the build integration (src/cli/src/commands/build_native.cpp).
// =============================================================================
#include "detail/generator/generator.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "cp3/diag/diagnostic_codes.hpp"
#include "cp3/host/native_bindings.hpp"
#include "detail/generator/cpp_writer.h"
#include "detail/generator/mangle.h"

namespace cp3 {

using namespace detail;

namespace {

// Does the recorded C++ parameter type take a `const char*`? A `const char*`
// parameter takes a cp3 string's C string view; a `const std::string&` takes
// the string itself.
bool takesCharPointer(const std::string& cppType) {
    return cppType.find("char*") != std::string::npos;
}

// Is this parameter read twice (a pointer and its length)? Then its argument
// must be evaluated once.
bool readTwice(const NativeParam& param) {
    if (param.cppArgCount < 2) return false;
    return param.kind == NativeParamKind::ByteBuffer ||
           param.kind == NativeParamKind::NumberArray;
}

// The member of one binding's enum, or "" when the binding does not carry the
// enum or the member.
std::string nativeEnumMemberOf(const NativeModule& module, const Type& receiver,
                                const std::string& property) {
    for (const NativeEnum& en : module.enums) {
        const Type type = nativeEnumType(module.id, en.cppQualified);
        if (type.name != receiver.name) continue;
        for (const NativeEnumMember& member : en.members) {
            if (member.name == property) {
                return en.cppQualified + "::" + member.cppName;
            }
        }
    }
    return std::string();
}

// A native enum's C++ member spelling (`demo::Color::Red`), or "" when the
// receiver is not a native enum or the member is unknown.
std::string nativeEnumMemberText(const std::vector<NativeBinding>& bindings,
                                 const Type& receiver,
                                 const std::string& property) {
    if (!isNativeEnumType(receiver)) return std::string();
    for (const NativeBinding& binding : bindings) {
        const std::string member = nativeEnumMemberOf(binding.module, receiver,
                                                       property);
        if (!member.empty()) return member;
    }
    return std::string();
}

}  // namespace

// The native bindings this unit imports, keyed by their local alias, plus the
// headers the generated .cpp must include.
void Generator::collectNativeImports(const Program& program) {
    for (const auto& stmtPtr : program.body) {
        if (!stmtPtr || stmtPtr->kind != NodeKind::ImportDeclaration) continue;
        const auto& imp = static_cast<const ImportDeclaration&>(*stmtPtr);
        if (imp.kind != ImportKind::Native) continue;
        for (const NativeBinding& binding : options_.nativeBindings) {
            if (binding.id != imp.source) continue;
            collectNativeBinding(binding, imp);
        }
    }
}

// One imported binding: the alias map, the headers and the enums.
void Generator::collectNativeBinding(const NativeBinding& binding,
                                      const ImportDeclaration& imp) {
    // The alias is the one this unit wrote: two units may import the same
    // binding under different names.
    const std::string alias = imp.alias.empty() ? imp.source : imp.alias;
    nativeAliases_[alias] = &binding.module;
    for (const std::string& header : binding.module.headers) {
        if (std::find(nativeHeaders_.begin(), nativeHeaders_.end(), header) ==
            nativeHeaders_.end()) {
            nativeHeaders_.push_back(header);
        }
    }
    collectNativeEnums(binding.module);
}

// The binding's enums join the enum table, so an enum value formats and
// prints like a cp3 `enum` member. The C++ enum class itself comes from the
// native library, so nothing is emitted for it.
void Generator::collectNativeEnums(const NativeModule& module) {
    for (const NativeEnum& en : module.enums) {
        EnumCppInfo info;
        info.name = nativeEnumType(module.id, en.cppQualified).name;
        info.cpp = en.cppQualified;
        for (std::size_t i = 0; i < en.members.size(); ++i) {
            EnumCppMember member;
            member.name = en.members[i].name;
            member.text = en.members[i].cppName;
            member.value = static_cast<long long>(i);
            info.members.push_back(std::move(member));
        }
        enums_.emplace(info.name, std::move(info));
    }
}

// The name of the temporary a twice-read argument is bound to.
std::string Generator::nativeTempName(std::size_t index) {
    return "cp3_native_arg" + std::to_string(index);
}

// The C++ arguments of one native call: one per recorded parameter, expanded
// to the C++ arguments the binding records (a buffer or number array
// parameter is a pointer plus a length).
std::vector<std::string> Generator::nativeArgTexts(
    const CallExpr& c, const NativeFunction& fn,
    const std::vector<std::string>& temporaries) {
    std::vector<std::string> out;
    out.reserve(fn.params.size());
    for (std::size_t i = 0; i < fn.params.size(); ++i) {
        const NativeParam& param = fn.params[i];
        Expr* arg = i < c.arguments.size() ? c.arguments[i].get() : nullptr;
        if (arg == nullptr) {
            out.push_back("0.0");
            continue;
        }
        // A twice-read argument is bound to a temporary first, so the pointer
        // and the length come from the same value.
        const bool temporary = !temporaries[i].empty();
        const std::string text =
            temporary ? temporaries[i] : expr(*arg);
        switch (param.kind) {
            case NativeParamKind::CStr:
                // A cp3 string is a std::string; a `const char*` parameter
                // takes its C string view, a `const std::string&` the value.
                if (takesCharPointer(param.cppType)) {
                    out.push_back("(" + text + ").c_str()");
                } else {
                    out.push_back(text);
                }
                break;
            case NativeParamKind::ByteBuffer:
                if (param.cppArgCount < 2) {
                    out.push_back(text);
                    break;
                }
                out.push_back(nativeBytesPointer(text));
                out.push_back("(" + text + ").bytes().size()");
                break;
            case NativeParamKind::NumberArray:
                if (param.cppArgCount < 2) {
                    out.push_back(text);
                    break;
                }
                out.push_back("(" + text + ")->data()");
                out.push_back("(" + text + ")->size()");
                break;
            default:
                out.push_back(text);
                break;
        }
    }
    return out;
}

// The `const unsigned char*` a byte-buffer parameter takes.
std::string Generator::nativeBytesPointer(const std::string& text) {
    return "reinterpret_cast<const unsigned char*>((" + text + ").bytes().data())";
}

// The cp3 spelling of a native result: a number result is a double, and a
// `const char*` result is the empty string for a null pointer.
std::string Generator::nativeResultText(const NativeFunction& fn,
                                         const std::string& call) {
    if (fn.returnsCStr) return "::cp3::runtime::nativeString(" + call + ")";
    if (fn.returnType.isNumber()) return "static_cast<double>(" + call + ")";
    return call;
}

// The call itself: the recorded C++ name, the recorded arguments, and the
// result mapping.
std::string Generator::emitNativeCall(const CallExpr& c, const MemberExpr& m) {
    if (!m.object || m.object->kind != NodeKind::Identifier) return std::string();
    const auto& id = static_cast<const Identifier&>(*m.object);
    const auto found = nativeAliases_.find(id.name);
    if (found == nativeAliases_.end()) return std::string();
    const NativeFunction* fn = findNativeFunction(*found->second, m.property);
    if (fn == nullptr) return std::string();
    // A class-typed parameter or result has no cp3 value model (spec 19): a
    // bound class is a declaration shape, not an object with an owner or a
    // lifetime. Refuse the call with the exact reason instead of emitting a
    // value nobody owns.
    for (const NativeParam& param : fn->params) {
        if (isNativeClassType(param.type)) {
            reportUnsupported(
                c, diagcode::kTypeMismatch,
                "type mismatch: native call '" + fn->name + "' cannot take '" +
                    param.name +
                    "' because a native class has no cp3 value model");
            return "0.0";
        }
    }
    if (isNativeClassType(fn->returnType)) {
        reportUnsupported(
            c, diagcode::kTypeMismatch,
            "type mismatch: native call '" + fn->name +
                "' cannot return a native class: a native class has no cp3 "
                "value model");
        return "0.0";
    }
    // A twice-read argument that is not a plain binding is bound to a
    // temporary, so it is evaluated once.
    std::vector<std::string> temporaries(fn->params.size());
    for (std::size_t i = 0; i < fn->params.size(); ++i) {
        if (!readTwice(fn->params[i])) continue;
        Expr* arg = i < c.arguments.size() ? c.arguments[i].get() : nullptr;
        if (arg == nullptr || arg->kind == NodeKind::Identifier) continue;
        temporaries[i] = nativeTempName(i);
    }
    const std::vector<std::string> args = nativeArgTexts(c, *fn, temporaries);
    const std::string call =
        fn->cppName + "(" + joinStrings(args, ", ") + ")";
    const bool bound = std::any_of(temporaries.begin(), temporaries.end(),
                                   [](const std::string& name) {
                                       return !name.empty();
                                   });
    if (!bound) return nativeResultText(*fn, call);
    // The temporaries live in a lambda, so the whole call is one expression.
    std::string prologue;
    for (std::size_t i = 0; i < temporaries.size(); ++i) {
        if (temporaries[i].empty()) continue;
        Expr* arg = c.arguments[i].get();
        prologue += "const auto& " + temporaries[i] + " = " + expr(*arg) + "; ";
    }
    return "[&]() { " + prologue + "return " + nativeResultText(*fn, call) +
           "; }()";
}

// A member read of a native binding: `dm.Color` is the enum's C++ name and
// `dm.Color.Red` its member.
std::string Generator::emitNativeMember(const MemberExpr& m) {
    if (!m.object) return std::string();
    const Type receiver = effectiveType(*m.object);
    if (isNativeEnumType(receiver)) {
        return nativeEnumMemberText(options_.nativeBindings, receiver, m.property);
    }
    if (m.object->kind != NodeKind::Identifier) return std::string();
    const auto& id = static_cast<const Identifier&>(*m.object);
    const auto found = nativeAliases_.find(id.name);
    if (found == nativeAliases_.end()) return std::string();
    const NativeEnum* en = findNativeEnum(*found->second, m.property);
    if (en == nullptr) return std::string();
    return en->cppQualified;
}

}  // namespace cp3
