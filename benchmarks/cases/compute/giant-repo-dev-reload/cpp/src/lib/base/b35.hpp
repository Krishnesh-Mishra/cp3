#ifndef GIANT_BASE_B35_HPP
#define GIANT_BASE_B35_HPP

inline int b35(int x) {
    int acc = x + 35;
    acc = (acc * 31 + 245) % 1000033;
    acc = (acc * 131 + x * 17 + 35) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B35_HPP
