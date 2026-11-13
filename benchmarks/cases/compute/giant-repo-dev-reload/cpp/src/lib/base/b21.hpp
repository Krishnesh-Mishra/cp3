#ifndef GIANT_BASE_B21_HPP
#define GIANT_BASE_B21_HPP

inline int b21(int x) {
    int acc = x + 21;
    acc = (acc * 31 + 147) % 1000033;
    acc = (acc * 131 + x * 17 + 21) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B21_HPP
