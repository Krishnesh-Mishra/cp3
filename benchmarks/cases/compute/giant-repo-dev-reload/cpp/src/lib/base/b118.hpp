#ifndef GIANT_BASE_B118_HPP
#define GIANT_BASE_B118_HPP

inline int b118(int x) {
    int acc = x + 118;
    acc = (acc * 31 + 826) % 1000033;
    acc = (acc * 131 + x * 17 + 118) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B118_HPP
