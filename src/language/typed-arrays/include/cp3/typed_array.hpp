// =============================================================================
// src/language/typed-arrays/include/cp3/typed_array.hpp - the JS-compat typed-array family
// (D69): the typed views.
//
// The 11 element tags and their coercion matrix, the fixed-size view type
// (`TypedArray<Tag>`: element count + byte offset over a hard `ArrayBuffer`),
// the constructors (`makeTypedArray` / `Of` / `From` / `FromArray` / `View`)
// and the JS method table (`fill` / `subarray` / `slice` / `copyWithin` /
// `reverse` / `indexOf` / `includes` / `join` / `set`), plus the whole-view
// rendering. The umbrella include also pulls cp3/array_extras.hpp (the
// regular-array extras) and cp3/data_view.hpp.
// =============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cp3/array_buffer.hpp"

namespace cp3::runtime {

// -----------------------------------------------------------------------------
// Element conversion (the per-class coercion matrix)
// -----------------------------------------------------------------------------

inline bool nonFinite(double v) { return std::isnan(v) || std::isinf(v); }

inline std::uint8_t toUint8(double v) {
    if (nonFinite(v)) return 0;
    double r = std::fmod(std::trunc(v), 256.0);
    if (r < 0) r += 256.0;
    return static_cast<std::uint8_t>(r);
}

inline std::int8_t toInt8(double v) {
    if (nonFinite(v)) return 0;
    double r = std::fmod(std::trunc(v), 256.0);
    if (r < 0) r += 256.0;
    if (r >= 128.0) r -= 256.0;
    return static_cast<std::int8_t>(r);
}

// ToUint8Clamp: NaN -> 0, round half to even (the default FP rounding mode).
inline std::uint8_t toUint8Clamped(double v) {
    if (std::isnan(v) || v <= 0.0) return 0;
    if (v >= 255.0) return 255;
    return static_cast<std::uint8_t>(std::nearbyint(v));
}

inline std::uint16_t toUint16(double v) {
    if (nonFinite(v)) return 0;
    double r = std::fmod(std::trunc(v), 65536.0);
    if (r < 0) r += 65536.0;
    return static_cast<std::uint16_t>(r);
}

inline std::int16_t toInt16(double v) {
    if (nonFinite(v)) return 0;
    double r = std::fmod(std::trunc(v), 65536.0);
    if (r < 0) r += 65536.0;
    if (r >= 32768.0) r -= 65536.0;
    return static_cast<std::int16_t>(r);
}

inline std::uint32_t toUint32(double v) {
    if (nonFinite(v)) return 0;
    double r = std::fmod(std::trunc(v), 4294967296.0);
    if (r < 0) r += 4294967296.0;
    return static_cast<std::uint32_t>(r);
}

inline std::int32_t toInt32(double v) {
    if (nonFinite(v)) return 0;
    double r = std::fmod(std::trunc(v), 4294967296.0);
    if (r < 0) r += 4294967296.0;
    if (r >= 2147483648.0) r -= 4294967296.0;
    return static_cast<std::int32_t>(r);
}

// 2^64 is not representable in a std::uint64_t, so the 64-bit wrap is computed
// in exact 2^32 chunks (fmod and floor are exact; each chunk fits a double).
inline std::uint64_t toUint64(double v) {
    if (nonFinite(v)) return 0;
    const double t = std::trunc(v);
    double lo = std::fmod(t, 4294967296.0);
    if (lo < 0) lo += 4294967296.0;
    double hi = std::fmod(std::floor(t / 4294967296.0), 4294967296.0);
    if (hi < 0) hi += 4294967296.0;
    return (static_cast<std::uint64_t>(hi) << 32) |
           static_cast<std::uint64_t>(lo);
}

inline std::int64_t toInt64(double v) {
    // C++20 defines the unsigned -> signed conversion as modulo 2^64.
    return static_cast<std::int64_t>(toUint64(v));
}

// -----------------------------------------------------------------------------
// The per-class tags
// -----------------------------------------------------------------------------

struct Int8Tag {
    using Stored = std::int8_t;
    static constexpr std::size_t bytes = 1;
    static const char* name() { return "Int8Array"; }
    static double read(std::int8_t v) { return static_cast<double>(v); }
    static std::int8_t write(double v) { return toInt8(v); }
};

struct Uint8Tag {
    using Stored = std::uint8_t;
    static constexpr std::size_t bytes = 1;
    static const char* name() { return "Uint8Array"; }
    static double read(std::uint8_t v) { return static_cast<double>(v); }
    static std::uint8_t write(double v) { return toUint8(v); }
};

struct Uint8ClampedTag {
    using Stored = std::uint8_t;
    static constexpr std::size_t bytes = 1;
    static const char* name() { return "Uint8ClampedArray"; }
    static double read(std::uint8_t v) { return static_cast<double>(v); }
    static std::uint8_t write(double v) { return toUint8Clamped(v); }
};

struct Int16Tag {
    using Stored = std::int16_t;
    static constexpr std::size_t bytes = 2;
    static const char* name() { return "Int16Array"; }
    static double read(std::int16_t v) { return static_cast<double>(v); }
    static std::int16_t write(double v) { return toInt16(v); }
};

struct Uint16Tag {
    using Stored = std::uint16_t;
    static constexpr std::size_t bytes = 2;
    static const char* name() { return "Uint16Array"; }
    static double read(std::uint16_t v) { return static_cast<double>(v); }
    static std::uint16_t write(double v) { return toUint16(v); }
};

struct Int32Tag {
    using Stored = std::int32_t;
    static constexpr std::size_t bytes = 4;
    static const char* name() { return "Int32Array"; }
    static double read(std::int32_t v) { return static_cast<double>(v); }
    static std::int32_t write(double v) { return toInt32(v); }
};

struct Uint32Tag {
    using Stored = std::uint32_t;
    static constexpr std::size_t bytes = 4;
    static const char* name() { return "Uint32Array"; }
    static double read(std::uint32_t v) { return static_cast<double>(v); }
    static std::uint32_t write(double v) { return toUint32(v); }
};

struct Float32Tag {
    using Stored = float;
    static constexpr std::size_t bytes = 4;
    static const char* name() { return "Float32Array"; }
    static double read(float v) { return static_cast<double>(v); }
    static float write(double v) { return static_cast<float>(v); }
};

struct Float64Tag {
    using Stored = double;
    static constexpr std::size_t bytes = 8;
    static const char* name() { return "Float64Array"; }
    static double read(double v) { return v; }
    static double write(double v) { return v; }
};

struct BigInt64Tag {
    using Stored = std::int64_t;
    static constexpr std::size_t bytes = 8;
    static const char* name() { return "BigInt64Array"; }
    static double read(std::int64_t v) { return static_cast<double>(v); }
    static std::int64_t write(double v) { return toInt64(v); }
};

struct BigUint64Tag {
    using Stored = std::uint64_t;
    static constexpr std::size_t bytes = 8;
    static const char* name() { return "BigUint64Array"; }
    static double read(std::uint64_t v) { return static_cast<double>(v); }
    static std::uint64_t write(double v) { return toUint64(v); }
};

// -----------------------------------------------------------------------------
// TypedArrayData: the fixed-size view over a buffer
// -----------------------------------------------------------------------------

template <class Tag>
class TypedArrayData {
public:
    using Stored = typename Tag::Stored;

    TypedArrayData(Ref<ArrayBuffer> buf, std::size_t offset, std::size_t len)
        : buffer(std::move(buf)), byteOffset(offset), length(len) {}

    std::size_t byteLength() const { return length * sizeof(Stored); }

    const std::uint8_t* bytesAt(std::size_t index) const {
        return buffer->data() + byteOffset + index * sizeof(Stored);
    }
    std::uint8_t* bytesAt(std::size_t index) {
        return buffer->data() + byteOffset + index * sizeof(Stored);
    }

    Stored load(std::size_t index) const {
        Stored out{};
        std::memcpy(&out, bytesAt(index), sizeof(Stored));
        return out;
    }
    void store(std::size_t index, Stored value) {
        std::memcpy(bytesAt(index), &value, sizeof(Stored));
    }

    Ref<ArrayBuffer> buffer;
    std::size_t byteOffset = 0;
    std::size_t length = 0;
};

template <class Tag>
using TypedArray = Ref<TypedArrayData<Tag>>;

// `new Int8Array(n)`: a fresh zero-filled buffer of n elements.
template <class Tag>
TypedArray<Tag> makeTypedArray(double length) {
    const std::size_t len =
        toIndex(length, "Invalid typed array length");
    auto buf = makeRef<ArrayBuffer>(len * sizeof(typename Tag::Stored));
    return makeRef<TypedArrayData<Tag>>(buf, 0, len);
}

// `new Int8Array([...])`: copy the values into a fresh buffer.
template <class Tag>
TypedArray<Tag> makeTypedArrayOf(std::initializer_list<double> values) {
    TypedArray<Tag> out = makeTypedArray<Tag>(static_cast<double>(values.size()));
    std::size_t i = 0;
    for (double v : values) out->store(i++, Tag::write(v));
    return out;
}

// `new Int8Array(numberArray)`: element copy from a cp3 `number[]`.
template <class Tag>
TypedArray<Tag> makeTypedArrayFromArray(const Array<double>& values) {
    TypedArray<Tag> out = makeTypedArray<Tag>(static_cast<double>(values->size()));
    for (std::size_t i = 0; i < values->size(); ++i) {
        out->store(i, Tag::write((*values)[i]));
    }
    return out;
}

// `new Int8Array(otherTyped)`: element copy across classes (or the same one).
template <class Tag, class SrcTag>
TypedArray<Tag> makeTypedArrayFrom(const TypedArray<SrcTag>& src) {
    TypedArray<Tag> out = makeTypedArray<Tag>(static_cast<double>(src->length));
    for (std::size_t i = 0; i < src->length; ++i) {
        out->store(i, Tag::write(SrcTag::read(src->load(i))));
    }
    return out;
}

// `new Int8Array(buffer)` / `(buffer, offset)` / `(buffer, offset, length)`.
template <class Tag>
TypedArray<Tag> makeTypedArrayView(const Ref<ArrayBuffer>& buffer,
                                   double byteOffset) {
    constexpr std::size_t kElement = sizeof(typename Tag::Stored);
    const std::size_t off = viewOffset(byteOffset, buffer->byteLength(), kElement);
    const std::size_t len =
        viewCount(buffer->byteLength(), off, kElement, std::nullopt);
    return makeRef<TypedArrayData<Tag>>(buffer, off, len);
}

template <class Tag>
TypedArray<Tag> makeTypedArrayView(const Ref<ArrayBuffer>& buffer,
                                   double byteOffset, double length) {
    constexpr std::size_t kElement = sizeof(typename Tag::Stored);
    const std::size_t off = viewOffset(byteOffset, buffer->byteLength(), kElement);
    const std::size_t len =
        viewCount(buffer->byteLength(), off, kElement, length);
    return makeRef<TypedArrayData<Tag>>(buffer, off, len);
}

// -----------------------------------------------------------------------------
// Indexed access
// -----------------------------------------------------------------------------

// An index is an element position only when it is a non-negative integer
// strictly inside the view (NaN, infinities, fractions and negatives are not).
inline bool indexInRange(double index, std::size_t length) {
    if (std::isnan(index) || std::isinf(index)) return false;
    const double t = std::trunc(index);
    return t == index && t >= 0.0 && t < static_cast<double>(length);
}

template <class Tag>
std::optional<double> typedArrayAt(const TypedArray<Tag>& a, double index) {
    if (!indexInRange(index, a->length)) return std::nullopt;
    return Tag::read(a->load(static_cast<std::size_t>(index)));
}

// The refined read: a statically in-range literal index; anything else panics.
template <class Tag>
double typedArrayAtChecked(const TypedArray<Tag>& a, double index) {
    if (!indexInRange(index, a->length)) typedArrayPanic("Index out of range");
    return Tag::read(a->load(static_cast<std::size_t>(index)));
}

// A write outside the view is ignored (JS), like a fractional or negative one.
template <class Tag>
void typedArrayWriteAt(const TypedArray<Tag>& a, double index, double value) {
    if (!indexInRange(index, a->length)) return;
    a->store(static_cast<std::size_t>(index), Tag::write(value));
}

// -----------------------------------------------------------------------------
// Methods (fill / subarray / slice / copyWithin / reverse / indexOf /
// includes / join / set)
// -----------------------------------------------------------------------------

// The relative-index helpers (relativeIndex/startIndex/endIndex) live in
// cp3/array_buffer.hpp, included above: the buffer `.slice` needs them too.

template <class Tag>
TypedArray<Tag> typedArrayFill(const TypedArray<Tag>& a, double value,
                               double start = 0.0,
                               std::optional<double> end = std::nullopt) {
    const typename Tag::Stored stored = Tag::write(value);
    const std::size_t from = startIndex(start, a->length);
    const std::size_t to = endIndex(end, a->length);
    for (std::size_t i = from; i < to; ++i) a->store(i, stored);
    return a;
}

// `subarray` shares the receiver's buffer and bytes.
template <class Tag>
TypedArray<Tag> typedArraySubarray(const TypedArray<Tag>& a, double begin = 0.0,
                                   std::optional<double> end = std::nullopt) {
    const std::size_t from = startIndex(begin, a->length);
    const std::size_t to = endIndex(end, a->length);
    const std::size_t count = to > from ? to - from : 0;
    return makeRef<TypedArrayData<Tag>>(
        a->buffer, a->byteOffset + from * sizeof(typename Tag::Stored), count);
}

// `slice` copies into a fresh buffer.
template <class Tag>
TypedArray<Tag> typedArraySlice(const TypedArray<Tag>& a, double begin = 0.0,
                                std::optional<double> end = std::nullopt) {
    const std::size_t from = startIndex(begin, a->length);
    const std::size_t to = endIndex(end, a->length);
    const std::size_t count = to > from ? to - from : 0;
    TypedArray<Tag> out = makeTypedArray<Tag>(static_cast<double>(count));
    for (std::size_t i = 0; i < count; ++i) out->store(i, a->load(from + i));
    return out;
}

template <class Tag>
TypedArray<Tag> typedArrayCopyWithin(const TypedArray<Tag>& a, double target,
                                     double start = 0.0,
                                     std::optional<double> end = std::nullopt) {
    const std::size_t to = relativeIndex(target, a->length);
    const std::size_t from = startIndex(start, a->length);
    const std::size_t last = endIndex(end, a->length);
    const std::size_t count =
        last > from ? std::min(last - from, a->length - to) : 0;
    if (count != 0) {
        std::memmove(a->bytesAt(to), a->bytesAt(from),
                     count * sizeof(typename Tag::Stored));
    }
    return a;
}

template <class Tag>
TypedArray<Tag> typedArrayReverse(const TypedArray<Tag>& a) {
    for (std::size_t i = 0, j = a->length; i + 1 < j; ++i, --j) {
        const typename Tag::Stored lo = a->load(i);
        a->store(i, a->load(j - 1));
        a->store(j - 1, lo);
    }
    return a;
}

// The result is the std signed-index width (`isize`/`ptrdiff_t`, canonical
// `i64`): an element position or -1 when absent. Codegen casts to `double`
// only in a TypeScript project. The `from` index keeps the JS `double`
// semantics (truncate, NaN -> 0, negatives count from the end).
template <class Tag>
std::int64_t typedArrayIndexOf(const TypedArray<Tag>& a, double value,
                               double from = 0.0) {
    for (std::size_t i = startIndex(from, a->length); i < a->length; ++i) {
        if (Tag::read(a->load(i)) == value) {
            return static_cast<std::int64_t>(i);
        }
    }
    return -1;
}

template <class Tag>
bool typedArrayIncludes(const TypedArray<Tag>& a, double value,
                        double from = 0.0) {
    for (std::size_t i = startIndex(from, a->length); i < a->length; ++i) {
        const double v = Tag::read(a->load(i));
        if (v == value || (std::isnan(v) && std::isnan(value))) return true;
    }
    return false;
}

template <class Tag>
std::string typedArrayJoin(const TypedArray<Tag>& a,
                           const std::string& sep = std::string(",")) {
    std::string out;
    for (std::size_t i = 0; i < a->length; ++i) {
        if (i != 0) out += sep;
        out += toString(Tag::read(a->load(i)));
    }
    return out;
}

// The `set` offset: ToIntegerOrInfinity semantics, then the whole source must
// fit (a mismatch panics, like the spec).
template <class Tag>
std::size_t setStart(const TypedArray<Tag>& a, std::size_t count, double offset) {
    if (std::isnan(offset)) offset = 0.0;
    if (offset < 0 || std::isinf(offset)) {
        typedArrayPanic("set offset out of range");
    }
    const double t = std::trunc(offset);
    if (t + static_cast<double>(count) > static_cast<double>(a->length)) {
        typedArrayPanic("set source is too large");
    }
    return static_cast<std::size_t>(t);
}

template <class Tag>
void typedArraySetFromArray(const TypedArray<Tag>& a, const Array<double>& src,
                            double offset = 0.0) {
    const std::size_t at = setStart(a, src->size(), offset);
    for (std::size_t i = 0; i < src->size(); ++i) {
        a->store(at + i, Tag::write((*src)[i]));
    }
}

template <class Tag, class SrcTag>
void typedArraySetFrom(const TypedArray<Tag>& a,
                       const TypedArray<SrcTag>& src, double offset = 0.0) {
    const std::size_t at = setStart(a, src->length, offset);
    for (std::size_t i = 0; i < src->length; ++i) {
        a->store(at + i, Tag::write(SrcTag::read(src->load(i))));
    }
}

// -----------------------------------------------------------------------------
// Rendering (console.log of a whole typed array): "Float32Array(2) [1.5, 2.5]"
// -----------------------------------------------------------------------------

template <class Tag>
std::string typedArrayText(const TypedArray<Tag>& a) {
    std::string out = std::string(Tag::name()) + "(" + std::to_string(a->length) +
                      ") [";
    for (std::size_t i = 0; i < a->length; ++i) {
        if (i != 0) out += ", ";
        out += toString(Tag::read(a->load(i)));
    }
    out += "]";
    return out;
}

}  // namespace cp3::runtime

// The umbrella: one include pulls the whole family (D69).
#include "cp3/array_extras.hpp"
#include "cp3/data_view.hpp"
