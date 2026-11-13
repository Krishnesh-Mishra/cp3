#ifndef GIANT_BASE_B13_HPP
#define GIANT_BASE_B13_HPP

inline int b13(int x) {
    int acc = x + 13;
    acc = (acc * 31 + 91) % 1000033;
    acc = (acc * 131 + x * 17 + 13) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B13_HPP
