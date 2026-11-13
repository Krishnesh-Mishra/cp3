#ifndef GIANT_BASE_B7_HPP
#define GIANT_BASE_B7_HPP

inline int b7(int x) {
    int acc = x + 7;
    acc = (acc * 31 + 49) % 1000033;
    acc = (acc * 131 + x * 17 + 7) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B7_HPP
