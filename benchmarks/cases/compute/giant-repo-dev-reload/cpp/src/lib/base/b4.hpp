#ifndef GIANT_BASE_B4_HPP
#define GIANT_BASE_B4_HPP

inline int b4(int x) {
    int acc = x + 4;
    acc = (acc * 31 + 28) % 1000033;
    acc = (acc * 131 + x * 17 + 4) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B4_HPP
