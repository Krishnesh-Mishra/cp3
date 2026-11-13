#ifndef GIANT_BASE_B78_HPP
#define GIANT_BASE_B78_HPP

inline int b78(int x) {
    int acc = x + 78;
    acc = (acc * 31 + 546) % 1000033;
    acc = (acc * 131 + x * 17 + 78) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B78_HPP
