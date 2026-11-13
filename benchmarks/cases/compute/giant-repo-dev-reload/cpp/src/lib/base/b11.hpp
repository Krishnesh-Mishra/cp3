#ifndef GIANT_BASE_B11_HPP
#define GIANT_BASE_B11_HPP

inline int b11(int x) {
    int acc = x + 11;
    acc = (acc * 31 + 77) % 1000033;
    acc = (acc * 131 + x * 17 + 11) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B11_HPP
