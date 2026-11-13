#ifndef GIANT_BASE_B2_HPP
#define GIANT_BASE_B2_HPP

inline int b2(int x) {
    int acc = x + 2;
    acc = (acc * 31 + 14) % 1000033;
    acc = (acc * 131 + x * 17 + 2) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B2_HPP
