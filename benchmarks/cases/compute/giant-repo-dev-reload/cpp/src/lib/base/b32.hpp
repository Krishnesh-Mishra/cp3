#ifndef GIANT_BASE_B32_HPP
#define GIANT_BASE_B32_HPP

inline int b32(int x) {
    int acc = x + 32;
    acc = (acc * 31 + 224) % 1000033;
    acc = (acc * 131 + x * 17 + 32) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B32_HPP
