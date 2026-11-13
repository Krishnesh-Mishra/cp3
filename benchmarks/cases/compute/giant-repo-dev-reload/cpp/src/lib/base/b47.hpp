#ifndef GIANT_BASE_B47_HPP
#define GIANT_BASE_B47_HPP

inline int b47(int x) {
    int acc = x + 47;
    acc = (acc * 31 + 329) % 1000033;
    acc = (acc * 131 + x * 17 + 47) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B47_HPP
