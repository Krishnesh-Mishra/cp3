#ifndef GIANT_BASE_B25_HPP
#define GIANT_BASE_B25_HPP

inline int b25(int x) {
    int acc = x + 25;
    acc = (acc * 31 + 175) % 1000033;
    acc = (acc * 131 + x * 17 + 25) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B25_HPP
