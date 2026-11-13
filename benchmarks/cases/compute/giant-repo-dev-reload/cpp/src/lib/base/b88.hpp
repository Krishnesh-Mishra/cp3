#ifndef GIANT_BASE_B88_HPP
#define GIANT_BASE_B88_HPP

inline int b88(int x) {
    int acc = x + 88;
    acc = (acc * 31 + 616) % 1000033;
    acc = (acc * 131 + x * 17 + 88) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B88_HPP
