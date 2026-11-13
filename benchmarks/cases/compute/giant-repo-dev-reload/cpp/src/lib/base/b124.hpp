#ifndef GIANT_BASE_B124_HPP
#define GIANT_BASE_B124_HPP

inline int b124(int x) {
    int acc = x + 124;
    acc = (acc * 31 + 868) % 1000033;
    acc = (acc * 131 + x * 17 + 124) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B124_HPP
