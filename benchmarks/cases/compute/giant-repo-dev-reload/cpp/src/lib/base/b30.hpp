#ifndef GIANT_BASE_B30_HPP
#define GIANT_BASE_B30_HPP

inline int b30(int x) {
    int acc = x + 30;
    acc = (acc * 31 + 210) % 1000033;
    acc = (acc * 131 + x * 17 + 30) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B30_HPP
