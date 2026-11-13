#ifndef GIANT_BASE_B22_HPP
#define GIANT_BASE_B22_HPP

inline int b22(int x) {
    int acc = x + 22;
    acc = (acc * 31 + 154) % 1000033;
    acc = (acc * 131 + x * 17 + 22) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B22_HPP
