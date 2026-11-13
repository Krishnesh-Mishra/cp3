#ifndef GIANT_BASE_B67_HPP
#define GIANT_BASE_B67_HPP

inline int b67(int x) {
    int acc = x + 67;
    acc = (acc * 31 + 469) % 1000033;
    acc = (acc * 131 + x * 17 + 67) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B67_HPP
