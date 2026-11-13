#ifndef GIANT_BASE_B40_HPP
#define GIANT_BASE_B40_HPP

inline int b40(int x) {
    int acc = x + 40;
    acc = (acc * 31 + 280) % 1000033;
    acc = (acc * 131 + x * 17 + 40) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B40_HPP
