// =============================================================================
// cli/private/deflate_tables.hpp — RFC 1951 length/distance tables.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header, shared by inflate.cpp
// and deflate.cpp so the two halves of the DEFLATE codec cannot drift apart.
//
// The tables are constants straight from RFC 1951 §3.2.5.
// =============================================================================
#pragma once

#include <cstdint>

namespace cp3::cli {

// Length codes 257..285: base length and extra-bit count per code index.
inline constexpr std::uint16_t kLenBase[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
    67, 83, 99, 115, 131, 163, 195, 227, 258};
inline constexpr std::uint8_t kLenExtra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
    4, 4, 4, 4, 5, 5, 5, 5, 0};

// Distance codes 0..29: base distance and extra-bit count per code index.
inline constexpr std::uint16_t kDistBase[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513,
    769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
inline constexpr std::uint8_t kDistExtra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8,
    9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

}  // namespace cp3::cli
