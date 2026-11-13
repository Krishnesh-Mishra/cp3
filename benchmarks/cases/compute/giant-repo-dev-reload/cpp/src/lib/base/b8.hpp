#ifndef GIANT_BASE_B8_HPP
#define GIANT_BASE_B8_HPP

inline int b8(int x) {
    int acc = x + 8;
    acc = (acc * 31 + 56) % 1000033;
    acc = (acc * 131 + x * 17 + 8) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B8_HPP
