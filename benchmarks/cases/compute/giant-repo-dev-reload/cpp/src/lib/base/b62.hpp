#ifndef GIANT_BASE_B62_HPP
#define GIANT_BASE_B62_HPP

inline int b62(int x) {
    int acc = x + 62;
    acc = (acc * 31 + 434) % 1000033;
    acc = (acc * 131 + x * 17 + 62) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B62_HPP
