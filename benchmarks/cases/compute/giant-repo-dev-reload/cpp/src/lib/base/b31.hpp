#ifndef GIANT_BASE_B31_HPP
#define GIANT_BASE_B31_HPP

inline int b31(int x) {
    int acc = x + 31;
    acc = (acc * 31 + 217) % 1000033;
    acc = (acc * 131 + x * 17 + 31) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B31_HPP
