#ifndef GIANT_BASE_B15_HPP
#define GIANT_BASE_B15_HPP

inline int b15(int x) {
    int acc = x + 15;
    acc = (acc * 31 + 105) % 1000033;
    acc = (acc * 131 + x * 17 + 15) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B15_HPP
