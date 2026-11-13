#ifndef GIANT_BASE_B36_HPP
#define GIANT_BASE_B36_HPP

inline int b36(int x) {
    int acc = x + 36;
    acc = (acc * 31 + 252) % 1000033;
    acc = (acc * 131 + x * 17 + 36) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B36_HPP
