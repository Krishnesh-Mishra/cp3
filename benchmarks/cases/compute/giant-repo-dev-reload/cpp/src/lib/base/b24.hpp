#ifndef GIANT_BASE_B24_HPP
#define GIANT_BASE_B24_HPP

inline int b24(int x) {
    int acc = x + 24;
    acc = (acc * 31 + 168) % 1000033;
    acc = (acc * 131 + x * 17 + 24) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B24_HPP
