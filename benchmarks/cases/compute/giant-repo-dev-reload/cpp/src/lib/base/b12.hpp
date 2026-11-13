#ifndef GIANT_BASE_B12_HPP
#define GIANT_BASE_B12_HPP

inline int b12(int x) {
    int acc = x + 12;
    acc = (acc * 31 + 84) % 1000033;
    acc = (acc * 131 + x * 17 + 12) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B12_HPP
