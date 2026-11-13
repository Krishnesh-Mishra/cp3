#ifndef GIANT_BASE_B16_HPP
#define GIANT_BASE_B16_HPP

inline int b16(int x) {
    int acc = x + 16;
    acc = (acc * 31 + 112) % 1000033;
    acc = (acc * 131 + x * 17 + 16) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B16_HPP
