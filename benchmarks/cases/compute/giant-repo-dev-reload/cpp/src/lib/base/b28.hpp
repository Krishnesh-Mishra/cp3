#ifndef GIANT_BASE_B28_HPP
#define GIANT_BASE_B28_HPP

inline int b28(int x) {
    int acc = x + 28;
    acc = (acc * 31 + 196) % 1000033;
    acc = (acc * 131 + x * 17 + 28) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B28_HPP
