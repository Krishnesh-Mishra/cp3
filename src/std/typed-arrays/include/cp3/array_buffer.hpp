// =============================================================================
// src/std/typed-arrays/include/cp3/array_buffer.hpp - ArrayBuffer / SharedArrayBuffer.
//
// The fixed-size backing store plus the view arithmetic (the offset/count
// range checks and the family's RangeError path) shared by the typed views,
// DataView and the buffer `.slice`. Part of the JS-compat typed-array family
// (D69); the typed views themselves live in cp3/typed_array.hpp.
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

#include "cp3/core/value.hpp"

namespace cp3::runtime {

// The family's spec-violation path. The corpus pins it as exit 1 (tests assert
// the exit code; stderr text stays spec prose).
[[noreturn]] inline void typedArrayPanic(const char* message) {
    std::fprintf(stderr, "cp3: RangeError: %s\n", message);
    std::fflush(stderr);
    std::exit(1);
}

// -----------------------------------------------------------------------------
// ArrayBuffer / SharedArrayBuffer
// -----------------------------------------------------------------------------

class ArrayBuffer {
public:
    explicit ArrayBuffer(std::size_t byteLength) : bytes_(byteLength) {}

    std::size_t byteLength() const { return bytes_.size(); }
    std::uint8_t* data() { return bytes_.data(); }
    const std::uint8_t* data() const { return bytes_.data(); }

private:
    std::vector<std::uint8_t> bytes_;
};

// A subclass, so a Ref<SharedArrayBuffer> converts to Ref<ArrayBuffer> (the
// view constructors take either).
class SharedArrayBuffer : public ArrayBuffer {
public:
    using ArrayBuffer::ArrayBuffer;
};

// `ToIndex` for a byte/element count: NaN -> 0, anything negative or infinite
// panics (the spec throws).
inline std::size_t toIndex(double v, const char* message) {
    if (std::isnan(v)) return 0;
    if (v < 0 || std::isinf(v)) typedArrayPanic(message);
    return static_cast<std::size_t>(std::trunc(v));
}

inline Ref<ArrayBuffer> makeArrayBuffer(double byteLength) {
    return makeRef<ArrayBuffer>(toIndex(byteLength, "Invalid ArrayBuffer length"));
}

inline Ref<SharedArrayBuffer> makeSharedArrayBuffer(double byteLength) {
    return makeRef<SharedArrayBuffer>(
        toIndex(byteLength, "Invalid SharedArrayBuffer length"));
}

// A view's byte offset: NaN -> 0, in range, aligned to the element size.
inline std::size_t viewOffset(double offsetV, std::size_t bufferBytes,
                              std::size_t elementBytes) {
    if (std::isnan(offsetV)) offsetV = 0.0;
    if (offsetV < 0 || std::isinf(offsetV)) {
        typedArrayPanic("Start offset out of range");
    }
    const double t = std::trunc(offsetV);
    if (t > static_cast<double>(bufferBytes)) {
        typedArrayPanic("Start offset is outside the buffer");
    }
    const std::size_t off = static_cast<std::size_t>(t);
    if (elementBytes != 0 && off % elementBytes != 0) {
        typedArrayPanic("Start offset is not aligned to the element size");
    }
    return off;
}

// A view's element count: the rest of the buffer, or the explicit length
// (which must fit the rest). Double math avoids overflow on huge lengths.
// The no-length form needs the rest to hold whole elements: a partial element
// at the end of the buffer is a RangeError in the reference semantics.
inline std::size_t viewCount(std::size_t bufferBytes, std::size_t off,
                             std::size_t elementBytes,
                             const std::optional<double>& lengthV) {
    const std::size_t remaining = bufferBytes - off;
    if (!lengthV) {
        if (elementBytes != 0 && remaining % elementBytes != 0) {
            typedArrayPanic(
                "Buffer length is not a multiple of the element size");
        }
        return remaining / elementBytes;
    }
    double lenV = *lengthV;
    if (std::isnan(lenV)) lenV = 0.0;
    if (lenV < 0 || std::isinf(lenV)) {
        typedArrayPanic("Invalid typed array length");
    }
    const double t = std::trunc(lenV);
    if (t * static_cast<double>(elementBytes) >
        static_cast<double>(remaining)) {
        typedArrayPanic("Length is outside the buffer");
    }
    return static_cast<std::size_t>(t);
}
// `ToIntegerOrInfinity`-style relative index: NaN -> 0, negatives count from
// the end, clamped to [0, length]. Shared by the buffer `.slice` here and the
// typed-view methods (fill/subarray/slice/copyWithin) in cp3/typed_array.hpp.
inline std::size_t relativeIndex(double v, std::size_t length) {
    if (std::isnan(v)) return 0;
    const double t = std::trunc(v);
    if (t < 0) {
        const double fromEnd = static_cast<double>(length) + t;
        return fromEnd <= 0 ? 0 : static_cast<std::size_t>(fromEnd);
    }
    return t >= static_cast<double>(length) ? length
                                            : static_cast<std::size_t>(t);
}

inline std::size_t startIndex(double v, std::size_t length) {
    return relativeIndex(v, length);
}

inline std::size_t endIndex(const std::optional<double>& v, std::size_t length) {
    return v ? relativeIndex(*v, length) : length;
}

inline Ref<ArrayBuffer> arrayBufferSlice(const Ref<ArrayBuffer>& buffer,
                                         double begin,
                                         std::optional<double> end = std::nullopt) {
    const std::size_t len = buffer->byteLength();
    const std::size_t from = startIndex(begin, len);
    const std::size_t to = endIndex(end, len);
    auto out = makeRef<ArrayBuffer>(to > from ? to - from : 0);
    if (to > from) {
        std::memcpy(out->data(), buffer->data() + from, to - from);
    }
    return out;
}

}  // namespace cp3::runtime
