// =============================================================================
// src/cli/src/native/bindgen_members.cpp - pass 2: free functions and class
// members.
//
// OWNERSHIP: cli module (native-install). Part of native/bindgen_internal.hpp.
// =============================================================================
#include <string>
#include <utility>
#include <vector>

#include "native/bindgen/bindgen_internal.hpp"

namespace cp3::cli::native {

// -----------------------------------------------------------------------------
// Pass 2: definitions
// -----------------------------------------------------------------------------
void Binder::collectDefinitions(CXCursor cursor,
                                const std::vector<std::string>& ns) {
    const CXCursorKind kind = lib_.clang_getCursorKind(cursor);
    if (kind == CXCursor_Namespace) {
        std::vector<std::string> nested = ns;
        const std::string name = cursorSpelling(cursor);
        if (!name.empty()) nested.push_back(name);
        std::pair<Binder*, const std::vector<std::string>*> context{this,
                                                                    &nested};
        lib_.clang_visitChildren(
            cursor,
            [](CXCursor child, CXCursor, CXClientData data) {
                auto* pair =
                    static_cast<std::pair<Binder*, const std::vector<std::string>*>*>(
                        data);
                pair->first->collectDefinitions(child, *pair->second);
                return CXChildVisit_Continue;
            },
            &context);
        return;
    }
    if (kind == CXCursor_FunctionDecl) {
        collectFunction(cursor, ns);
        return;
    }
    if (kind == CXCursor_ClassDecl || kind == CXCursor_StructDecl ||
        kind == CXCursor_UnionDecl) {
        const auto found = classIndex_.find(qualifiedName(cursor));
        if (found != classIndex_.end() && !classes_[found->second].rejected) {
            collectClassMembers(classes_[found->second]);
        }
    }
}

void Binder::collectFunction(CXCursor cursor,
                             const std::vector<std::string>& ns) {
    if (!inBoundHeader(cursor)) return;
    const std::string name = cursorSpelling(cursor);
    const std::string named = "function '" + qualifiedName(cursor) + "'";
    if (name.empty() || name.rfind("operator", 0) == 0) {
        rejectCursor(cursor, named,
                     name.empty() ? "unnamed function" :
                                    "operator overloading is not supported");
        return;
    }
    const auto count = functionCounts_.find(qualifiedName(cursor));
    if (count != functionCounts_.end() && count->second > 1) {
        rejectCursor(cursor, named,
                     "overload sets are not supported in beta one");
        return;
    }
    if (lib_.clang_Cursor_isVariadic(cursor)) {
        rejectCursor(cursor, named,
                     "variadic functions are not supported");
        return;
    }
    NativeFunction fn;
    fn.name = cp3SafeIdent(name);
    fn.cppName = qualifiedName(cursor);
    if (!collectSignature(cursor, fn)) return;
    if (usedNames_.count(fn.name) != 0) {
        std::string flattened;
        for (const std::string& part : ns) flattened += part + "_";
        fn.name = uniqueName(flattened + cp3SafeIdent(name), "function");
    } else {
        usedNames_.insert(fn.name);
    }
    module_.functions.push_back(std::move(fn));
}

void Binder::collectClassMembers(ClassDecl& decl) {
    const CXCursor cursor = decl.cursor;
    struct CountContext {
        Binder* binder;
        std::unordered_map<std::string, int>* counts;
        std::unordered_set<std::string>* seen;
    };
    std::unordered_map<std::string, int> overloads;
    std::unordered_set<std::string> seen;
    CountContext counting{this, &overloads, &seen};
    lib_.clang_visitChildren(
        cursor,
        [](CXCursor child, CXCursor, CXClientData data) {
            auto* context = static_cast<CountContext*>(data);
            const CXCursorKind kind =
                context->binder->lib_.clang_getCursorKind(child);
            if (kind != CXCursor_CXXMethod &&
                kind != CXCursor_ConversionFunction) {
                return CXChildVisit_Continue;
            }
            if (!context->seen->insert(context->binder->cursorUSR(child)).second) {
                return CXChildVisit_Continue;
            }
            ++(*context->counts)[context->binder->cursorSpelling(child)];
            return CXChildVisit_Continue;
        },
        &counting);

    struct MemberContext {
        Binder* binder;
        ClassDecl* decl;
        std::unordered_map<std::string, int>* counts;
    };
    MemberContext context{this, &decl, &overloads};
    lib_.clang_visitChildren(
        cursor,
        [](CXCursor child, CXCursor, CXClientData data) {
            auto* member = static_cast<MemberContext*>(data);
            const CXCursorKind kind =
                member->binder->lib_.clang_getCursorKind(child);
            if (kind == CXCursor_FieldDecl) {
                member->binder->collectField(*member->decl, child);
            } else if (kind == CXCursor_CXXMethod ||
                       kind == CXCursor_ConversionFunction) {
                const std::string name =
                    member->binder->cursorSpelling(child);
                member->binder->collectMethod(*member->decl, child,
                                              (*member->counts)[name]);
            }
            return CXChildVisit_Continue;
        },
        &context);
}

void Binder::collectField(ClassDecl& decl, CXCursor cursor) {
    const CX_CXXAccessSpecifier access =
        lib_.clang_getCXXAccessSpecifier(cursor);
    if (access != CX_CXXPublic && access != CX_CXXInvalidAccessSpecifier) return;
    const std::string name = cursorSpelling(cursor);
    const MappedType mapped = mapType(lib_.clang_getCursorType(cursor), false);
    if (!mapped.ok || mapped.rawPointer) {
        rejectCursor(cursor, "field '" + decl.model.cppQualified + "::" + name + "'",
                     mapped.ok ? "raw pointer fields are not supported"
                               : mapped.reason);
        return;
    }
    NativeField field;
    field.name = cp3SafeIdent(name);
    field.cppName = name;
    field.type = mapped.type;
    field.cppType = mapped.cppType;
    decl.model.fields.push_back(std::move(field));
}

void Binder::collectMethod(ClassDecl& decl, CXCursor cursor, int overloads) {
    const CXCursorKind kind = lib_.clang_getCursorKind(cursor);
    const std::string name = cursorSpelling(cursor);
    if (kind == CXCursor_CXXMethod && (name.empty() ||
        name.rfind("operator", 0) == 0)) {
        rejectCursor(cursor, "method '" + decl.model.cppQualified + "::" + name + "'",
                     "operator overloading is not supported");
        return;
    }
    if (kind != CXCursor_CXXMethod) {
        if (kind == CXCursor_ConversionFunction) {
            rejectCursor(cursor,
                         "conversion operator in '" + decl.model.cppQualified + "'",
                         "operator overloading is not supported");
        }
        return;
    }
    const CX_CXXAccessSpecifier access =
        lib_.clang_getCXXAccessSpecifier(cursor);
    if (access != CX_CXXPublic && access != CX_CXXInvalidAccessSpecifier) return;
    if (overloads > 1) {
        rejectCursor(cursor, "method '" + decl.model.cppQualified + "::" + name + "'",
                     "overload sets are not supported in beta one");
        return;
    }
    if (lib_.clang_Cursor_isVariadic(cursor)) {
        rejectCursor(cursor, "method '" + decl.model.cppQualified + "::" + name + "'",
                     "variadic methods are not supported");
        return;
    }
    NativeFunction fn;
    fn.name = cp3SafeIdent(name);
    fn.cppName = name;
    fn.isStatic = lib_.clang_CXXMethod_isStatic(cursor) != 0;
    if (!collectSignature(cursor, fn)) return;
    if (decl.model.findMethod(fn.name) != nullptr) {
        rejectCursor(cursor, "method '" + decl.model.cppQualified + "::" + name + "'",
                     "overload sets are not supported in beta one");
        return;
    }
    decl.model.methods.push_back(std::move(fn));
}

bool Binder::collectSignature(CXCursor cursor, NativeFunction& out) {
    const MappedType ret = mapType(lib_.clang_getCursorResultType(cursor), false);
    if (!ret.ok || ret.rawPointer) {
        rejectCursor(cursor, "function '" + qualifiedName(cursor) + "'",
                     ret.ok ? "returned pointers are not supported"
                            : ret.reason);
        return false;
    }
    out.returnType = mappedTypeOf(ret);
    out.returnsCStr = ret.returnsCStr;

    const int count = lib_.clang_getNumArgTypes(lib_.clang_getCursorType(cursor));
    for (int i = 0; i < count; ++i) {
        CXType type = lib_.clang_getArgType(lib_.clang_getCursorType(cursor), i);
        NativeParam param;
        const CXCursor argCursor = lib_.clang_Cursor_getArgument(cursor, i);
        const std::string argName = lib_.clang_Cursor_isNull(argCursor)
                                        ? std::string()
                                        : cursorSpelling(argCursor);
        param.name =
            argName.empty() ? "arg" + std::to_string(i) : cp3SafeIdent(argName);
        param.cppType = typeSpelling(type);
        MappedType mapped = mapType(type, true);
        if (!mapped.ok) {
            rejectCursor(cursor, "function '" + qualifiedName(cursor) + "'",
                         "parameter " + std::to_string(i) + ": " + mapped.reason);
            return false;
        }
        if (mapped.rawPointer) {
            if (i + 1 >= count ||
                !isIntegral(lib_.clang_getArgType(
                    lib_.clang_getCursorType(cursor), i + 1))) {
                rejectCursor(cursor, "function '" + qualifiedName(cursor) + "'",
                             "parameter " + std::to_string(i) +
                                 ": pointer parameter is not followed by a length");
                return false;
            }
            const bool bytes = mapped.kind == NativeParamKind::ByteBuffer;
            param.kind = mapped.kind;
            param.type = bytes ? Type::named(TypeKind::Class, "Buffer")
                               : Type::array(Type::number());
            param.cppPointer = mapped.cppPointer;
            param.cppArgCount = 2;
            out.params.push_back(std::move(param));
            ++i;
            continue;
        }
        param.kind = mapped.kind;
        param.type = mapped.type;
        out.params.push_back(std::move(param));
    }
    return true;
}

std::string Binder::typeSpelling(CXType type) const {
    CXString text = lib_.clang_getTypeSpelling(type);
    const char* c = lib_.clang_getCString(text);
    std::string out = c == nullptr ? std::string() : std::string(c);
    lib_.clang_disposeString(text);
    return out;
}

}  // namespace cp3::cli::native
