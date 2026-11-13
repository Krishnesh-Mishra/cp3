#ifndef GIANT_BASE_B43_HPP
#define GIANT_BASE_B43_HPP

inline int b43(int x) {
    int acc = x + 43;
    acc = (acc * 31 + 301) % 1000033;
    acc = (acc * 131 + x * 17 + 43) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B43_HPP
