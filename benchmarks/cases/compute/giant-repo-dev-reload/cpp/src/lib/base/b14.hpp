#ifndef GIANT_BASE_B14_HPP
#define GIANT_BASE_B14_HPP

inline int b14(int x) {
    int acc = x + 14;
    acc = (acc * 31 + 98) % 1000033;
    acc = (acc * 131 + x * 17 + 14) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B14_HPP
