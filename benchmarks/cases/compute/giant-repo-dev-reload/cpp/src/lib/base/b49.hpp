#ifndef GIANT_BASE_B49_HPP
#define GIANT_BASE_B49_HPP

inline int b49(int x) {
    int acc = x + 49;
    acc = (acc * 31 + 343) % 1000033;
    acc = (acc * 131 + x * 17 + 49) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B49_HPP
