#ifndef GIANT_BASE_B10_HPP
#define GIANT_BASE_B10_HPP

inline int b10(int x) {
    int acc = x + 10;
    acc = (acc * 31 + 70) % 1000033;
    acc = (acc * 131 + x * 17 + 10) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B10_HPP
