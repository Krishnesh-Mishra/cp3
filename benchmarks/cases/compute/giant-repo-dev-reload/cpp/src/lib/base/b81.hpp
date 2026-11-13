#ifndef GIANT_BASE_B81_HPP
#define GIANT_BASE_B81_HPP

inline int b81(int x) {
    int acc = x + 81;
    acc = (acc * 31 + 567) % 1000033;
    acc = (acc * 131 + x * 17 + 81) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B81_HPP
