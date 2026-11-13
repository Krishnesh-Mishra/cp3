#ifndef GIANT_BASE_B42_HPP
#define GIANT_BASE_B42_HPP

inline int b42(int x) {
    int acc = x + 42;
    acc = (acc * 31 + 294) % 1000033;
    acc = (acc * 131 + x * 17 + 42) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B42_HPP
