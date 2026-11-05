// =============================================================================
// src/std/typed-arrays/codegen_methods.cpp - the JS-compat typed-array family
// (D69): the member, property and index surface.
//
// `fill` / `join` / `includes` on regular arrays and typed views, the typed
// method table (`subarray` / `slice` / `copyWithin` / `reverse` / `indexOf` /
// `includes` / `set`), the `DataView` `get*` / `set*` calls, the property reads
// (`length`, `byteLength`, `byteOffset`, `buffer`, `X.BYTES_PER_ELEMENT`) and
// the indexed reads / writes. The constructors and statics live in the sibling
// codegen.cpp.
// =============================================================================
#include "detail/generator/generator.h"

#include <cstddef>
#include <string>

#include "detail/generator/family.h"
#include "detail/parallel/parallel.h"
#include "detail/generator/type_map.h"
#include "cp3/diag/diagnostic_codes.hpp"

namespace cp3 {

using namespace detail;

namespace {

// The element-type gate's diagnostic. An array whose element type has no C++
// spelling cannot be lowered - cp3 has no dynamic array (docs 09) - so the
// site reports the type error the array declaration sites report (CP3401),
// not a missing-feature skip: the program is ill-typed, and the fix is to
// name the element type.
std::string elementTypeMessage(const Type& elem, const std::string& what) {
    if (elem.isUnknown()) {
        return "cannot infer the element type of " + what + "; annotate the binding";
    }
    return "type mismatch: an array cannot hold elements of type '" +
           describe(elem) + "'";
}

// The comparator a `sort` call takes: two elements in, a number out (the JS
// comparator contract, docs 09). A named comparator is checked here, because
// sema treats every function-typed argument as compatible with a
// function-typed parameter (the structural check is deferred).
bool sortComparatorMatches(const Type& fnType, const CppInfo& elem) {
    if (fnType.kind != TypeKind::Function || fnType.parts.size() != 3) {
        return false;
    }
    for (std::size_t i = 0; i < 2; ++i) {
        const CppInfo param = classify(fnType.parts[i]);
        if (!param.representable || param.cpp != elem.cpp) return false;
    }
    const CppInfo ret = classify(fnType.parts[2]);
    return ret.representable &&
           (ret.shape == Shape::Number || ret.shape == Shape::Native);
}

// The cp3 type label of an index receiver, for the not-indexable diagnostic:
// the class/enum name when the type has one, the kind's stable name
// otherwise ("number", "array", "object", ...).
std::string typeLabel(const Type& t) {
    if (!t.name.empty()) return t.name;
    return typeKindName(t.kind);
}

}  // namespace

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
            reportTypeError(c, elementTypeMessage(elem, "a fill"));
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
        reportTypeError(c, elementTypeMessage(arrayElemOf(recv), "a join"));
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

// `arr.sort()` / `arr.sort((a, b) => number)` (docs/language-spec/09):
// in place, stable, returns the receiver. The default ordering is type-aware
// (numeric for number arrays, lexicographic for string arrays); the callback
// form lowers the arrow with the element type as its parameter context.
std::string Generator::arraySortCallText(const CallExpr& c, const MemberExpr& m,
                                         const Type& recv) {
    if (m.property != "sort" || recv.kind != TypeKind::Array) {
        return std::string();
    }
    CppInfo ec;
    if (!arrayElementInfo(arrayElemOf(recv), ec)) {
        reportTypeError(c, elementTypeMessage(arrayElemOf(recv), "a sort"));
        return "0.0";
    }
    needsTypedArray_ = true;
    const std::string recvText =
        familyReceiverText(*m.object, arrayElemOf(recv), c);
    if (c.arguments.empty()) {
        return "::cp3::runtime::arraySort<" + ec.cpp + ">(" + recvText + ")";
    }
    const Expr* arg = c.arguments.front().get();
    if (arg == nullptr) {
        // Call arguments are never holes (the parser rejects `a.sort(,)`), so
        // a missing comparator is a compiler invariant; report it instead of
        // emitting a call with no comparator.
        reportUnsupported(c, diagcode::kTypeMismatch,
                          "type mismatch: the sort comparator must be a "
                          "function");
        return "0.0";
    }
    if (arg->kind == NodeKind::ArrowFunction) {
        const std::string cmp =
            emitArrow(static_cast<const ArrowFunction&>(*arg), {ec.cpp, ec.cpp});
        return "::cp3::runtime::arraySortWith<" + ec.cpp + ">(" + recvText + ", " +
               cmp + ")";
    }
    // A named comparator (`a.sort(cmp)`): a function value the runtime calls
    // with the two elements. It must take the element type and return a
    // number; sema accepts any function-typed argument here (the structural
    // function check is deferred), so the signature is checked in codegen.
    const CppInfo ci = info(*arg);
    if (ci.shape != Shape::Function || !sortComparatorMatches(ci.type, ec)) {
        reportTypeError(c, "type mismatch: the sort comparator must take two "
                           "elements of the array's element type and return a "
                           "number");
        return "0.0";
    }
    return "::cp3::runtime::arraySortWith<" + ec.cpp + ">(" + recvText + ", " +
           expr(*arg) + ")";
}

// `arr.includes(value, from?)` (docs/language-spec/09-arrays-strings.md): the
// SameValueZero search over the elements. The regular-array emitter owns this
// method (including the optional fromIndex); this family hook keeps the
// element-type gate for a receiver that emitter did not lower.
std::string Generator::arrayIncludesCallText(const CallExpr& c,
                                             const MemberExpr& m,
                                             const Type& recv) {
    if (m.property != "includes" || recv.kind != TypeKind::Array) {
        return std::string();
    }
    if (c.arguments.empty() || c.arguments.size() > 2) {
        reportUnsupported(c, diagcode::kArityMismatch,
                          "arity mismatch: includes() expects 1 or 2 arguments, "
                          "got " +
                              std::to_string(c.arguments.size()));
        return "0.0";
    }
    if (!c.arguments.front()) {
        reportUnsupported(c, diagcode::kTypeMismatch,
                          "type mismatch: includes() needs a value to search "
                          "for");
        return "0.0";
    }
    CppInfo ec;
    if (!arrayElementInfo(arrayElemOf(recv), ec)) {
        reportTypeError(c, elementTypeMessage(arrayElemOf(recv), "an includes"));
        return "0.0";
    }
    needsTypedArray_ = true;
    std::string out = "::cp3::runtime::arrayIncludes<" + ec.cpp + ">(" +
                      familyReceiverText(*m.object, arrayElemOf(recv), c) +
                      ", " + expr(*c.arguments.front());
    if (c.arguments.size() > 1) {
        out += ", ";
        out += c.arguments[1] ? expr(*c.arguments[1]) : "0.0";
    }
    return out + ")";
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
    // Relative-index parameters keep the runtime's `double` JS semantics
    // (truncate/NaN/negative-from-end): a Rust/C++ project's signed-index
    // argument converts there; a TypeScript project's `number` already is
    // that `double`. No cast is emitted (a cast would lose the JS rules).
    for (const auto& arg : c.arguments) {
        out += ", ";
        out += arg ? expr(*arg) : "0.0";
    }
    out += ")";
    // `indexOf` returns the exact signed-index width; a TypeScript project
    // keeps `number`, so the read widens there.
    if (prop == "indexOf" &&
        options_.typeSystem == TypeSystem::TypeScript) {
        return "static_cast<double>(" + out + ")";
    }
    return out;
}

// The DataView `get*`/`set*` surface; the runtime keeps the big-endian
// default (only an explicit `true` flips it).
std::string Generator::dataViewCallText(const CallExpr& c, const MemberExpr& m) {
    const auto fastRead = fastDataViewReads_.find(c.nodeId);
    if (fastRead != fastDataViewReads_.end()) {
        const auto active = activeFastDataViews_.find(fastRead->second.receiver);
        if (active != activeFastDataViews_.end() &&
            m.property == (fastRead->second.float64 ? "getFloat64" : "getUint8") &&
            fastRead->second.offset) {
            const std::string address = active->second +
                " + static_cast<std::size_t>(" + expr(*fastRead->second.offset) + ")";
            if (!fastRead->second.float64) {
                return "*reinterpret_cast<const std::uint8_t*>(" + address + ")";
            }
            return "([&]() { double cp3_fast_value; std::memcpy(&cp3_fast_value, " +
                   address + ", sizeof(cp3_fast_value)); return cp3_fast_value; }())";
        }
    }
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
    if (recv.kind == TypeKind::Array) {
        if (m.property == "join") return arrayJoinCallText(c, m, recv);
        if (m.property == "sort") return arraySortCallText(c, m, recv);
        if (m.property == "includes") return arrayIncludesCallText(c, m, recv);
    }
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
                // The std count width; a TypeScript project keeps `number`.
                if (options_.typeSystem == TypeSystem::TypeScript) {
                    return "static_cast<double>(" + std::to_string(fc->bytes) +
                           ")";
                }
                return std::to_string(fc->bytes);
            }
        }
    }
    const Type recv = effectiveType(*m.object);
    if (m.property == "length") {
        if (isParallelRunType(recv)) {
            // A `$parallelRun` result is a fixed-extent `std::array` (the
            // runtime sized it from the callable count); `.length` is its
            // extent, exactly like an ordinary array's.
            needsTypedArray_ = true;
            return "static_cast<double>((" + expr(*m.object) + ").size())";
        }
        if (recv.kind == TypeKind::Array && info(*m.object).representable) {
            needsTypedArray_ = true;
            return "static_cast<double>((" + expr(*m.object) + ")->size())";
        }
        if (recv.kind == TypeKind::Class && familyTag(recv.name) != nullptr) {
            needsTypedArray_ = true;
            // The exact std length width (`usize`/`size_t`); a TypeScript
            // project keeps `number`, so the read widens there. (The
            // regular-array and parallel-run branches above stay `double`:
            // their typing is not this family's.)
            const std::string read =
                "((" + expr(*m.object) + ")->length)";
            if (options_.typeSystem == TypeSystem::TypeScript) {
                return "static_cast<double>(" + read + ")";
            }
            return read;
        }
        return std::string();
    }
    if (recv.kind != TypeKind::Class) return std::string();
    const bool isView = familyTag(recv.name) != nullptr;
    const bool isBuffer =
        recv.name == "ArrayBuffer" || recv.name == "SharedArrayBuffer";
    const bool isDataView = recv.name == "DataView";
    // The exact std length/index widths; a TypeScript project widens here.
    auto readWidth = [this](const std::string& read) {
        if (options_.typeSystem == TypeSystem::TypeScript) {
            return "static_cast<double>(" + read + ")";
        }
        return read;
    };
    if (m.property == "byteLength") {
        if (isView || isBuffer) {
            needsTypedArray_ = true;
            return readWidth("((" + expr(*m.object) + ")->byteLength())");
        }
        if (isDataView) {
            needsTypedArray_ = true;
            return readWidth("((" + expr(*m.object) + ")->byteLength)");
        }
        return std::string();
    }
    if (m.property == "byteOffset") {
        if (isView || isDataView) {
            needsTypedArray_ = true;
            return readWidth("((" + expr(*m.object) + ")->byteOffset)");
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

// The index operand of a family read/write. A number-backed enum member is
// its underlying number (spec 04); a string-backed enum, or an enum whose
// declaration is missing from this unit, reports instead of emitting an
// operand C++ cannot convert.
std::string Generator::indexOperandText(const Expr& index) {
    const CppInfo ci = info(index);
    if (ci.shape != Shape::Enum) return expr(index);
    const auto found = enums_.find(ci.type.name);
    if (found == enums_.end()) {
        // The enum's declaration is not in this unit (a top-level enum, an
        // imported one and a function-local one are all collected), so the
        // member has no C++ spelling: the operand is a type error, not a
        // missing feature.
        reportTypeError(index, "type mismatch: the enum '" + ci.type.name +
                                   "' is not declared in this unit");
        return "0.0";
    }
    if (found->second.stringBacked) {
        // A string-backed enum member has no numeric value: its C++ spelling
        // is an ordinal, so it cannot be an index operand. The decision of
        // 2026-10-19 says the read reports instead of emitting a
        // non-convertible operand - and a report is a type error, not a
        // missing feature.
        reportTypeError(index,
                        "type mismatch: a string-backed enum member cannot be "
                        "used as an array index");
        return "0.0";
    }
    return "static_cast<double>(" + expr(index) + ")";
}

// `a[i]`: sema published `number` when the literal index was proved in range
// (checked read), `number | undefined` otherwise (`std::optional`).
std::string Generator::emitFamilyIndexRead(const IndexExpr& ix) {
    if (!ix.object || !ix.index) return std::string();
    Type ot = effectiveType(*ix.object);
    if (ot.isLiteral()) ot = ot.literalBaseType();
    const std::string idxText = indexOperandText(*ix.index);
    if (isParallelRunType(ot)) {
        // The `parallel.run` result: a fixed-extent `std::array` (the runtime
        // sized it from the callable count), so the index is a raw subscript.
        return "(" + expr(*ix.object) + ")[static_cast<std::size_t>(" + idxText +
               ")]";
    }
    // A tuple's constant index read (`p[1]` -> `std::get<1>(p)`); "" otherwise.
    if (std::string tuple = emitGenericIndexRead(ix); !tuple.empty()) {
        return tuple;
    }
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
            reportTypeError(ix, elementTypeMessage(arrayElemOf(ot),
                                                   "an element read"));
            return "0.0";
        }
        needsTypedArray_ = true;
        std::string elementCpp = ec.cpp;
        if (isStackValueArray(*ix.object)) {
            constexpr const char* refPrefix = "::cp3::runtime::Ref<";
            const std::size_t prefixSize =
                std::char_traits<char>::length(refPrefix);
            if (elementCpp.rfind(refPrefix, 0) == 0 &&
                elementCpp.size() > prefixSize && elementCpp.back() == '>') {
                elementCpp = elementCpp.substr(
                    prefixSize, elementCpp.size() - prefixSize - 1);
            }
        }
        return "::cp3::runtime::arrayAt<" + elementCpp + ">(" + expr(*ix.object) +
               ", " + idxText + ")";
    }
    if (ot.kind == TypeKind::String) {
        // Docs 09: the raw UTF-8 byte at a byte position. The runtime
        // returns the exact std byte-value width (`u8`); a TypeScript
        // project keeps `number`, so the read widens there.
        needsString_ = true;
        const std::string read = "::cp3::runtime::strings::byteAt(" +
                                 expr(*ix.object) + ", " + idxText + ")";
        if (options_.typeSystem == TypeSystem::TypeScript) {
            return "static_cast<double>(" + read + ")";
        }
        return read;
    }
    return std::string();
}

// The `IndexExpr` dispatch (expr.cpp): the family read when the receiver is
// one, otherwise the type error for a receiver with no index operation.
std::string Generator::emitIndexExpr(const IndexExpr& ix) {
    std::string value = emitFamilyIndexRead(ix);
    if (!value.empty()) return value;
    Type recv = ix.object ? effectiveType(*ix.object) : Type::unknown();
    if (recv.isLiteral()) recv = recv.literalBaseType();
    if (recv.isError()) {
        // Sema already reported the ill-typed receiver; a second diagnostic
        // for the same webserverion would only duplicate it.
        return "0.0";
    }
    if (recv.isUnknown()) {
        // Sema reports this form with CP3414 (an unknown receiver has no
        // index operation), so the codegen fallback carries the same code.
        reportUnsupported(ix, diagcode::kUnknownType,
                          "cannot index into type 'unknown'");
        return "0.0";
    }
    // The receiver carries no index operation at all (a class instance, a
    // `Map`/`Set`, a namespace object): the read is a program error, not a
    // missing feature, so it gets the not-indexable diagnostic.
    reportCodegenError(ix, diagcode::kNotIndexable,
                       "value of type '" + typeLabel(recv) +
                           "' is not indexable (an index read is defined on "
                           "arrays, strings and typed arrays)");
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
    return familyIndexWriteText(ix, expr(*a.value));
}

// The element write of a family index target, with the assigned value
// already spelled (the `for...of` loop stores the element it bound).
std::string Generator::familyIndexWriteText(const IndexExpr& ix,
                                            const std::string& value) {
    if (!ix.object || !ix.index) return std::string();
    Type ot = effectiveType(*ix.object);
    if (ot.isLiteral()) ot = ot.literalBaseType();
    const std::string idxText = indexOperandText(*ix.index);
    if (isParallelRunType(ot)) {
        // A `parallel.run` result element write: `std::array` subscript.
        return "((" + expr(*ix.object) + ")[static_cast<std::size_t>(" + idxText +
               ")] = " + value + ")";
    }
    if (ot.kind == TypeKind::Class) {
        const char* tag = familyTag(ot.name);
        if (tag == nullptr) return std::string();
        needsTypedArray_ = true;
        const auto* receiver = ix.object->kind == NodeKind::Identifier
            ? &static_cast<const Identifier&>(*ix.object) : nullptr;
        const auto binding = receiver
            ? sema_.resolvedBindings.find(receiver->nodeId)
            : sema_.resolvedBindings.end();
        const auto fast = fastTypedArrayWriteSites_.find(ix.nodeId);
        if (receiver && binding != sema_.resolvedBindings.end() &&
            fast != fastTypedArrayWriteSites_.end() &&
            fast->second.receiverBinding == binding->second) {
            const auto active = activeFastTypedArrays_.find(binding->second);
            if (active != activeFastTypedArrays_.end() &&
                fast->second.width != 0 && fast->second.index) {
                const std::string stored = "::cp3::runtime::" +
                    fast->second.tag + "::write(static_cast<double>(" + value + "))";
                const std::string offset = "static_cast<std::size_t>(" +
                    expr(*fast->second.index) + ") * " +
                    std::to_string(fast->second.width);
                return "([&]() { const auto cp3_fast_stored = " + stored +
                       "; std::memcpy(" + active->second + " + (" + offset +
                       "), &cp3_fast_stored, sizeof(cp3_fast_stored)); }())";
            }
        }
        return "::cp3::runtime::typedArrayWriteAt<::cp3::runtime::" +
               std::string(tag) + ">(" + expr(*ix.object) + ", " + idxText +
               ", " + value + ")";
    }
    if (ot.kind == TypeKind::Array) {
        CppInfo ec;
        if (!arrayElementInfo(arrayElemOf(ot), ec)) {
            reportTypeError(ix, elementTypeMessage(arrayElemOf(ot),
                                                   "an element write"));
            return "0.0";
        }
        needsTypedArray_ = true;
        return "::cp3::runtime::arrayWriteAt<" + ec.cpp + ">(" +
               expr(*ix.object) + ", " + idxText + ", " + value + ")";
    }
    return std::string();
}

}  // namespace cp3
