#ifndef GIANT_BASE_B54_HPP
#define GIANT_BASE_B54_HPP

inline int b54(int x) {
    int acc = x + 54;
    acc = (acc * 31 + 378) % 1000033;
    acc = (acc * 131 + x * 17 + 54) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B54_HPP
