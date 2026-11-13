#ifndef GIANT_BASE_B116_HPP
#define GIANT_BASE_B116_HPP

inline int b116(int x) {
    int acc = x + 116;
    acc = (acc * 31 + 812) % 1000033;
    acc = (acc * 131 + x * 17 + 116) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B116_HPP
