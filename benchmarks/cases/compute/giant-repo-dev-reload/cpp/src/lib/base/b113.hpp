#ifndef GIANT_BASE_B113_HPP
#define GIANT_BASE_B113_HPP

inline int b113(int x) {
    int acc = x + 113;
    acc = (acc * 31 + 791) % 1000033;
    acc = (acc * 131 + x * 17 + 113) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B113_HPP
