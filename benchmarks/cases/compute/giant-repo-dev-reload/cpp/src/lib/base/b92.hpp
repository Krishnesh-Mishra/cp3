#ifndef GIANT_BASE_B92_HPP
#define GIANT_BASE_B92_HPP

inline int b92(int x) {
    int acc = x + 92;
    acc = (acc * 31 + 644) % 1000033;
    acc = (acc * 131 + x * 17 + 92) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B92_HPP
