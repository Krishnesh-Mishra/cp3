#ifndef GIANT_BASE_B101_HPP
#define GIANT_BASE_B101_HPP

inline int b101(int x) {
    int acc = x + 101;
    acc = (acc * 31 + 707) % 1000033;
    acc = (acc * 131 + x * 17 + 101) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B101_HPP
