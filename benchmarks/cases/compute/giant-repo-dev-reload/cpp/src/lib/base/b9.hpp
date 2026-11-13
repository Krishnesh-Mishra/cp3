#ifndef GIANT_BASE_B9_HPP
#define GIANT_BASE_B9_HPP

inline int b9(int x) {
    int acc = x + 9;
    acc = (acc * 31 + 63) % 1000033;
    acc = (acc * 131 + x * 17 + 9) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B9_HPP
