#ifndef GIANT_BASE_B84_HPP
#define GIANT_BASE_B84_HPP

inline int b84(int x) {
    int acc = x + 84;
    acc = (acc * 31 + 588) % 1000033;
    acc = (acc * 131 + x * 17 + 84) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B84_HPP
