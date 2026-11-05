// =============================================================================
// src/language/typed-arrays/include/cp3/data_view.hpp - DataView (D69).
//
// The unaligned byte-wise view: `makeDataView` (offset / length defaults), the
// `dvGet*` / `dvSet*` families and the big-endian default the corpus pins (an
// explicit `true` littleEndian flips a multi-byte access). Part of the
// JS-compat typed-array family; pulled by cp3/typed_array.hpp.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <bit>
#include <optional>
#include <type_traits>

#include "cp3/array_buffer.hpp"

namespace cp3::runtime {

// -----------------------------------------------------------------------------
// DataView (unaligned access is byte-wise; big-endian by default)
// -----------------------------------------------------------------------------

class DataView {
public:
    DataView(Ref<ArrayBuffer> buf, std::size_t off, std::size_t len)
        : buffer(std::move(buf)), byteOffset(off), byteLength(len),
          rawData(off == 0 ? buffer->data() : buffer->data() + off) {}

    Ref<ArrayBuffer> buffer;
    std::size_t byteOffset = 0;
    std::size_t byteLength = 0;
    // ArrayBuffer's storage cannot resize. Keep its byte base alongside the
    // owning Ref so repeated DataView reads avoid following buffer->data().
    std::uint8_t* const rawData;
};

inline Ref<DataView> makeDataView(const Ref<ArrayBuffer>& buffer) {
    return makeRef<DataView>(buffer, std::size_t(0), buffer->byteLength());
}

inline Ref<DataView> makeDataView(const Ref<ArrayBuffer>& buffer,
                                  double byteOffset) {
    const std::size_t off = viewOffset(byteOffset, buffer->byteLength(), 1);
    return makeRef<DataView>(buffer, off, buffer->byteLength() - off);
}

inline Ref<DataView> makeDataView(const Ref<ArrayBuffer>& buffer,
                                  double byteOffset, double byteLength) {
    const std::size_t off = viewOffset(byteOffset, buffer->byteLength(), 1);
    const std::size_t len =
        viewCount(buffer->byteLength(), off, 1, byteLength);
    return makeRef<DataView>(buffer, off, len);
}

// The byte range of an access, after the offset and the width are checked
// against the view (not the buffer: DataView bounds are view-relative).
inline std::uint8_t* dvBytes(const Ref<DataView>& dv, double offset,
                             std::size_t width) {
    if (std::isnan(offset)) offset = 0.0;
    if (offset < 0 || std::isinf(offset)) {
        typedArrayPanic("DataView offset out of range");
    }
    const double t = std::trunc(offset);
    if (t + static_cast<double>(width) > static_cast<double>(dv->byteLength)) {
        typedArrayPanic("DataView access is outside the view");
    }
    return dv->rawData + static_cast<std::size_t>(t);
}

// Native integer offsets need no JS number conversion (truncation, NaN and
// infinity checks). Keep the view bounds check, but do it without converting
// the induction variable to double. This overload is especially useful for
// generated counted loops over a DataView.
template <class Integer,
          std::enable_if_t<std::is_integral_v<Integer>, int> = 0>
inline std::uint8_t* dvBytes(const Ref<DataView>& dv, Integer offset,
                             std::size_t width) {
    if constexpr (std::is_signed_v<Integer>) {
        if (offset < 0) typedArrayPanic("DataView offset out of range");
    }
    const std::uintmax_t start = static_cast<std::uintmax_t>(offset);
    if (start > dv->byteLength || width > dv->byteLength - start) {
        typedArrayPanic("DataView access is outside the view");
    }
    return dv->rawData + static_cast<std::size_t>(start);
}

inline std::uint64_t dvLoad(const std::uint8_t* p, std::size_t width,
                            bool littleEndian) {
    std::uint64_t out = 0;
    for (std::size_t i = 0; i < width; ++i) {
        const std::size_t shift = littleEndian ? i : (width - 1 - i);
        out |= static_cast<std::uint64_t>(p[i]) << (8 * shift);
    }
    return out;
}

inline void dvStore(std::uint8_t* p, std::size_t width, std::uint64_t value,
                    bool littleEndian) {
    for (std::size_t i = 0; i < width; ++i) {
        const std::size_t shift = littleEndian ? i : (width - 1 - i);
        p[i] = static_cast<std::uint8_t>((value >> (8 * shift)) & 0xFFu);
    }
}

inline double dvGetUint8(const Ref<DataView>& dv, double offset) {
    return static_cast<double>(dvLoad(dvBytes(dv, offset, 1), 1, false));
}

template <class Integer,
          std::enable_if_t<std::is_integral_v<Integer>, int> = 0>
inline double dvGetUint8(const Ref<DataView>& dv, Integer offset) {
    return static_cast<double>(dvLoad(dvBytes(dv, offset, 1), 1, false));
}

inline double dvGetInt8(const Ref<DataView>& dv, double offset) {
    return static_cast<double>(
        static_cast<std::int8_t>(dvLoad(dvBytes(dv, offset, 1), 1, false)));
}

inline double dvGetUint16(const Ref<DataView>& dv, double offset,
                          bool littleEndian = false) {
    return static_cast<double>(dvLoad(dvBytes(dv, offset, 2), 2, littleEndian));
}

inline double dvGetInt16(const Ref<DataView>& dv, double offset,
                         bool littleEndian = false) {
    return static_cast<double>(
        static_cast<std::int16_t>(dvLoad(dvBytes(dv, offset, 2), 2, littleEndian)));
}

inline double dvGetUint32(const Ref<DataView>& dv, double offset,
                          bool littleEndian = false) {
    return static_cast<double>(dvLoad(dvBytes(dv, offset, 4), 4, littleEndian));
}

inline double dvGetInt32(const Ref<DataView>& dv, double offset,
                         bool littleEndian = false) {
    return static_cast<double>(
        static_cast<std::int32_t>(dvLoad(dvBytes(dv, offset, 4), 4, littleEndian)));
}

inline double dvGetFloat32(const Ref<DataView>& dv, double offset,
                           bool littleEndian = false) {
    const std::uint32_t bits = static_cast<std::uint32_t>(
        dvLoad(dvBytes(dv, offset, 4), 4, littleEndian));
    float f = 0.0f;
    std::memcpy(&f, &bits, sizeof(f));
    return static_cast<double>(f);
}

inline double dvGetFloat64(const Ref<DataView>& dv, double offset,
                           bool littleEndian = false) {
    const std::uint64_t bits = dvLoad(dvBytes(dv, offset, 8), 8, littleEndian);
    double d = 0.0;
    std::memcpy(&d, &bits, sizeof(d));
    return d;
}

template <class Integer,
          std::enable_if_t<std::is_integral_v<Integer>, int> = 0>
inline double dvGetFloat64(const Ref<DataView>& dv, Integer offset,
                           bool littleEndian = false) {
    const std::uint64_t bits =
        dvLoad(dvBytes(dv, offset, 8), 8, littleEndian);
    double d = 0.0;
    std::memcpy(&d, &bits, sizeof(d));
    return d;
}

inline void dvSetUint8(const Ref<DataView>& dv, double offset, double value) {
    dvStore(dvBytes(dv, offset, 1), 1, toUint8(value), false);
}

inline void dvSetInt8(const Ref<DataView>& dv, double offset, double value) {
    dvStore(dvBytes(dv, offset, 1), 1, static_cast<std::uint8_t>(toInt8(value)),
            false);
}

inline void dvSetUint16(const Ref<DataView>& dv, double offset, double value,
                        bool littleEndian = false) {
    dvStore(dvBytes(dv, offset, 2), 2, toUint16(value), littleEndian);
}

inline void dvSetInt16(const Ref<DataView>& dv, double offset, double value,
                       bool littleEndian = false) {
    dvStore(dvBytes(dv, offset, 2), 2, static_cast<std::uint16_t>(toInt16(value)),
            littleEndian);
}

inline void dvSetUint32(const Ref<DataView>& dv, double offset, double value,
                        bool littleEndian = false) {
    dvStore(dvBytes(dv, offset, 4), 4, toUint32(value), littleEndian);
}

inline void dvSetInt32(const Ref<DataView>& dv, double offset, double value,
                       bool littleEndian = false) {
    dvStore(dvBytes(dv, offset, 4), 4, static_cast<std::uint32_t>(toInt32(value)),
            littleEndian);
}

inline void dvSetFloat32(const Ref<DataView>& dv, double offset, double value,
                         bool littleEndian = false) {
    const float f = static_cast<float>(value);
    std::uint32_t bits = 0;
    std::memcpy(&bits, &f, sizeof(bits));
    dvStore(dvBytes(dv, offset, 4), 4, bits, littleEndian);
}

inline void dvSetFloat64(const Ref<DataView>& dv, double offset, double value,
                         bool littleEndian = false) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    dvStore(dvBytes(dv, offset, 8), 8, bits, littleEndian);
}

}  // namespace cp3::runtime
