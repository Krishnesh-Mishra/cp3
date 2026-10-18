// =============================================================================
// src/codegen/src/emit/tarrays_methods.cpp - the JS-compat typed-array family
// (D69): the member, property and index surface.
//
// `fill` / `join` on regular arrays and typed views, the typed method table
// (`subarray` / `slice` / `copyWithin` / `reverse` / `indexOf` / `includes` /
// `set`), the `DataView` `get*` / `set*` calls, the property reads (`length`,
// `byteLength`, `byteOffset`, `buffer`, `X.BYTES_PER_ELEMENT`) and the indexed
// reads / writes. The constructors and statics live in the sibling tarrays.cpp.
// =============================================================================
#include "detail/generator.h"

#include <cstddef>
#include <string>

#include "detail/family.h"
#include "detail/type_map.h"

namespace cp3 {

using namespace detail;

// =============================================================================
// Methods
// =============================================================================
// `arr.fill(value, start?, end?)`: in place, returning the receiver. On a
// regular array the element type is the *call's* (an `Array(n)` receiver is
// fixed by the fill, `Array(3).fill("x")`).
std::string Generator::arrayFillCallText(const CallExpr& c, const MemberExpr& m,
                                         const Type& recv) {
    if (recv.kind == TypeKind::Array) {
        const Type result = effectiveType(c);
        const Type elem = result.kind == TypeKind::Array ? arrayElemOf(result)
                                                         : arrayElemOf(recv);
        CppInfo ec;
        if (!arrayElementInfo(elem, ec)) {
            notImplemented(c, "the array fill element type");
            return "0.0";
        }
        needsTypedArray_ = true;
        std::string out = "::cp3::runtime::arrayFill<" + ec.cpp + ">(" +
                          familyReceiverText(*m.object, elem, c);
        for (const auto& arg : c.arguments) {
            out += ", ";
            out += arg ? expr(*arg) : "0.0";
        }
        return out + ")";
    }
    const char* tag =
        recv.kind == TypeKind::Class ? familyTag(recv.name) : nullptr;
    if (tag == nullptr) return std::string();
    needsTypedArray_ = true;
    std::string out = "::cp3::runtime::typedArrayFill<::cp3::runtime::" +
                      std::string(tag) + ">(" + expr(*m.object);
    for (const auto& arg : c.arguments) {
        out += ", ";
        out += arg ? expr(*arg) : "0.0";
    }
    return out + ")";
}

// `arr.join(sep)` on a regular array (sema pins the separator as required
// there; the typed surface alone allows the no-argument form).
std::string Generator::arrayJoinCallText(const CallExpr& c, const MemberExpr& m,
                                         const Type& recv) {
    if (m.property != "join" || recv.kind != TypeKind::Array) {
        return std::string();
    }
    CppInfo ec;
    if (!arrayElementInfo(arrayElemOf(recv), ec)) {
        notImplemented(c, "the array join element type");
        return "0.0";
    }
    needsTypedArray_ = true;
    const std::string recvText =
        familyReceiverText(*m.object, arrayElemOf(recv), c);
    const std::string sep =
        !c.arguments.empty() && c.arguments.front()
            ? expr(*c.arguments.front())
            : "std::string(\",\")";
    return "::cp3::runtime::arrayJoin<" + ec.cpp + ">(" + recvText + ", " + sep +
           ")";
}

// The typed-view method table; `set` dispatches on the source kind.
std::string Generator::typedMethodCallText(const CallExpr& c,
                                           const MemberExpr& m,
                                           const char* tag) {
    const std::string t = "::cp3::runtime::" + std::string(tag);
    const std::string recv = expr(*m.object);
    const std::string& prop = m.property;
    if (prop == "set") {
        if (c.arguments.empty() || !c.arguments.front()) return std::string();
        const Shape src = info(*c.arguments.front()).shape;
        const char* fn = src == Shape::TypedArray ? "typedArraySetFrom"
                         : src == Shape::Array  ? "typedArraySetFromArray"
                                                : nullptr;
        if (fn == nullptr) return std::string();
        needsTypedArray_ = true;
        std::string out = "::cp3::runtime::" + std::string(fn) + "<" + t + ">(" +
                          recv + ", " + expr(*c.arguments.front());
        for (std::size_t i = 1; i < c.arguments.size(); ++i) {
            out += ", ";
            out += c.arguments[i] ? expr(*c.arguments[i]) : "0.0";
        }
        return out + ")";
    }
    const char* fn = nullptr;
    if (prop == "subarray") fn = "typedArraySubarray";
    else if (prop == "slice") fn = "typedArraySlice";
    else if (prop == "copyWithin") fn = "typedArrayCopyWithin";
    else if (prop == "reverse") fn = "typedArrayReverse";
    else if (prop == "indexOf") fn = "typedArrayIndexOf";
    else if (prop == "includes") fn = "typedArrayIncludes";
    else if (prop == "join") fn = "typedArrayJoin";
    if (fn == nullptr) return std::string();
    needsTypedArray_ = true;
    std::string out = "::cp3::runtime::" + std::string(fn) + "<" + t + ">(" +
                      recv;
    for (const auto& arg : c.arguments) {
        out += ", ";
        out += arg ? expr(*arg) : "0.0";
    }
    return out + ")";
}

// The DataView `get*`/`set*` surface; the runtime keeps the big-endian
// default (only an explicit `true` flips it).
std::string Generator::dataViewCallText(const CallExpr& c, const MemberExpr& m) {
    static const char* const kWidths[] = {"Uint8", "Int8",  "Uint16", "Int16",
                                          "Uint32", "Int32", "Float32",
                                          "Float64"};
    const std::string& prop = m.property;
    const bool isGet = prop.compare(0, 3, "get") == 0;
    const bool isSet = prop.compare(0, 3, "set") == 0;
    if (!isGet && !isSet) return std::string();
    const std::string width = prop.substr(3);
    bool known = false;
    for (const char* w : kWidths) {
        if (width == w) known = true;
    }
    if (!known) return std::string();
    needsTypedArray_ = true;
    std::string out = "::cp3::runtime::dv" +
                      std::string(isGet ? "Get" : "Set") + width + "(" +
                      expr(*m.object);
    for (const auto& arg : c.arguments) {
        out += ", ";
        out += arg ? expr(*arg) : "0.0";
    }
    return out + ")";
}

// The family member-call dispatcher: `Array.of/from`, the array methods, the
// typed-view methods, the buffer `.slice` and the DataView surface.
std::string Generator::emitFamilyMemberCall(const CallExpr& c,
                                            const MemberExpr& m) {
    if (!m.object) return std::string();
    if (m.object->kind == NodeKind::Identifier &&
        static_cast<const Identifier&>(*m.object).name == "Array" &&
        !lookup("Array")) {
        return arrayStaticCallText(c, m.property);
    }
    const Type recv = effectiveType(*m.object);
    if (m.property == "fill") return arrayFillCallText(c, m, recv);
    if (recv.kind == TypeKind::Array) return arrayJoinCallText(c, m, recv);
    if (recv.kind == TypeKind::Class) {
        if (const char* tag = familyTag(recv.name)) {
            return typedMethodCallText(c, m, tag);
        }
        if (recv.name == "ArrayBuffer" || recv.name == "SharedArrayBuffer") {
            if (m.property != "slice") return std::string();
            needsTypedArray_ = true;
            std::string out =
                "::cp3::runtime::arrayBufferSlice(" + expr(*m.object);
            for (const auto& arg : c.arguments) {
                out += ", ";
                out += arg ? expr(*arg) : "0.0";
            }
            return out + ")";
        }
        if (recv.name == "DataView") return dataViewCallText(c, m);
    }
    return std::string();
}

// Property reads: `.length`, `.byteLength`, `.byteOffset`, `.buffer` and the
// static `X.BYTES_PER_ELEMENT`.
std::string Generator::emitFamilyMember(const MemberExpr& m) {
    if (!m.object) return std::string();
    if (m.object->kind == NodeKind::Identifier) {
        const std::string name = static_cast<const Identifier&>(*m.object).name;
        if (m.property == "BYTES_PER_ELEMENT" && !lookup(name)) {
            const FamilyClass* fc = familyClass(name);
            if (fc != nullptr && fc->bytes > 0) {
                needsTypedArray_ = true;
                return "static_cast<double>(" + std::to_string(fc->bytes) + ")";
            }
        }
    }
    const Type recv = effectiveType(*m.object);
    if (m.property == "length") {
        if (recv.kind == TypeKind::Array && info(*m.object).representable) {
            needsTypedArray_ = true;
            return "static_cast<double>((" + expr(*m.object) + ")->size())";
        }
        if (recv.kind == TypeKind::Class && familyTag(recv.name) != nullptr) {
            needsTypedArray_ = true;
            return "static_cast<double>((" + expr(*m.object) + ")->length)";
        }
        return std::string();
    }
    if (recv.kind != TypeKind::Class) return std::string();
    const bool isView = familyTag(recv.name) != nullptr;
    const bool isBuffer =
        recv.name == "ArrayBuffer" || recv.name == "SharedArrayBuffer";
    const bool isDataView = recv.name == "DataView";
    if (m.property == "byteLength") {
        if (isView || isBuffer) {
            needsTypedArray_ = true;
            return "static_cast<double>((" + expr(*m.object) +
                   ")->byteLength())";
        }
        if (isDataView) {
            needsTypedArray_ = true;
            return "static_cast<double>((" + expr(*m.object) + ")->byteLength)";
        }
        return std::string();
    }
    if (m.property == "byteOffset") {
        if (isView || isDataView) {
            needsTypedArray_ = true;
            return "static_cast<double>((" + expr(*m.object) + ")->byteOffset)";
        }
        return std::string();
    }
    if (m.property == "buffer") {
        if (isView || isDataView) {
            needsTypedArray_ = true;
            return "(" + expr(*m.object) + ")->buffer";
        }
    }
    return std::string();
}

// `a[i]`: sema published `number` when the literal index was proved in range
// (checked read), `number | undefined` otherwise (`std::optional`).
std::string Generator::emitFamilyIndexRead(const IndexExpr& ix) {
    if (!ix.object || !ix.index) return std::string();
    const Type ot = effectiveType(*ix.object);
    const std::string idxText = expr(*ix.index);
    if (ot.kind == TypeKind::Class) {
        const char* tag = familyTag(ot.name);
        if (tag == nullptr) return std::string();
        needsTypedArray_ = true;
        const std::string t = "::cp3::runtime::" + std::string(tag);
        const std::string recv = expr(*ix.object);
        if (info(ix).shape == Shape::Optional) {
            return "::cp3::runtime::typedArrayAt<" + t + ">(" + recv + ", " +
                   idxText + ")";
        }
        return "::cp3::runtime::typedArrayAtChecked<" + t + ">(" + recv + ", " +
               idxText + ")";
    }
    if (ot.kind == TypeKind::Array) {
        CppInfo ec;
        if (!arrayElementInfo(arrayElemOf(ot), ec)) {
            notImplemented(ix, "the array index read");
            return "0.0";
        }
        needsTypedArray_ = true;
        return "::cp3::runtime::arrayAt<" + ec.cpp + ">(" + expr(*ix.object) +
               ", " + idxText + ")";
    }
    return std::string();
}

// The `IndexExpr` dispatch (expr.cpp): the family read when the receiver is
// one, otherwise the ordinary not-implemented report.
std::string Generator::emitIndexExpr(const IndexExpr& ix) {
    std::string value = emitFamilyIndexRead(ix);
    if (!value.empty()) return value;
    notImplemented(ix, "expression");
    return "0.0";
}

// `a[i] = v`: a typed write ignores an out-of-range index (JS); a cp3 array
// write is bounds-checked.
std::string Generator::emitFamilyIndexWrite(const AssignmentExpr& a) {
    if (!a.target || a.target->kind != NodeKind::IndexExpr ||
        a.op != AssignOp::Assign || !a.value) {
        return std::string();
    }
    const auto& ix = static_cast<const IndexExpr&>(*a.target);
    if (!ix.object || !ix.index) return std::string();
    const Type ot = effectiveType(*ix.object);
    const std::string idxText = expr(*ix.index);
    const std::string value = expr(*a.value);
    if (ot.kind == TypeKind::Class) {
        const char* tag = familyTag(ot.name);
        if (tag == nullptr) return std::string();
        needsTypedArray_ = true;
        return "::cp3::runtime::typedArrayWriteAt<::cp3::runtime::" +
               std::string(tag) + ">(" + expr(*ix.object) + ", " + idxText +
               ", " + value + ")";
    }
    if (ot.kind == TypeKind::Array) {
        CppInfo ec;
        if (!arrayElementInfo(arrayElemOf(ot), ec)) {
            notImplemented(a, "the array element write");
            return "0.0";
        }
        needsTypedArray_ = true;
        return "::cp3::runtime::arrayWriteAt<" + ec.cpp + ">(" +
               expr(*ix.object) + ", " + idxText + ", " + value + ")";
    }
    return std::string();
}

}  // namespace cp3
