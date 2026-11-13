#ifndef GIANT_BASE_B38_HPP
#define GIANT_BASE_B38_HPP

inline int b38(int x) {
    int acc = x + 38;
    acc = (acc * 31 + 266) % 1000033;
    acc = (acc * 131 + x * 17 + 38) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B38_HPP
