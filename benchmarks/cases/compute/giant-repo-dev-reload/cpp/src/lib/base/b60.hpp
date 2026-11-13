#ifndef GIANT_BASE_B60_HPP
#define GIANT_BASE_B60_HPP

inline int b60(int x) {
    int acc = x + 60;
    acc = (acc * 31 + 420) % 1000033;
    acc = (acc * 131 + x * 17 + 60) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B60_HPP
