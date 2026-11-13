#ifndef GIANT_BASE_B63_HPP
#define GIANT_BASE_B63_HPP

inline int b63(int x) {
    int acc = x + 63;
    acc = (acc * 31 + 441) % 1000033;
    acc = (acc * 131 + x * 17 + 63) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B63_HPP
