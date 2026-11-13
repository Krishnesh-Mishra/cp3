#ifndef GIANT_BASE_B72_HPP
#define GIANT_BASE_B72_HPP

inline int b72(int x) {
    int acc = x + 72;
    acc = (acc * 31 + 504) % 1000033;
    acc = (acc * 131 + x * 17 + 72) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B72_HPP
