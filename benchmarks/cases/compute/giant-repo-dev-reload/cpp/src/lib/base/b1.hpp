#ifndef GIANT_BASE_B1_HPP
#define GIANT_BASE_B1_HPP

inline int b1(int x) {
    int acc = x + 1;
    acc = (acc * 31 + 7) % 1000033;
    acc = (acc * 131 + x * 17 + 1) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B1_HPP
