#ifndef GIANT_BASE_B120_HPP
#define GIANT_BASE_B120_HPP

inline int b120(int x) {
    int acc = x + 120;
    acc = (acc * 31 + 840) % 1000033;
    acc = (acc * 131 + x * 17 + 120) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B120_HPP
