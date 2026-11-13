#ifndef GIANT_BASE_B6_HPP
#define GIANT_BASE_B6_HPP

inline int b6(int x) {
    int acc = x + 6;
    acc = (acc * 31 + 42) % 1000033;
    acc = (acc * 131 + x * 17 + 6) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B6_HPP
