#ifndef GIANT_BASE_B70_HPP
#define GIANT_BASE_B70_HPP

inline int b70(int x) {
    int acc = x + 70;
    acc = (acc * 31 + 490) % 1000033;
    acc = (acc * 131 + x * 17 + 70) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B70_HPP
