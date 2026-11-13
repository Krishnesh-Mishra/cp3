#ifndef GIANT_BASE_B5_HPP
#define GIANT_BASE_B5_HPP

inline int b5(int x) {
    int acc = x + 5;
    acc = (acc * 31 + 35) % 1000033;
    acc = (acc * 131 + x * 17 + 5) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B5_HPP
