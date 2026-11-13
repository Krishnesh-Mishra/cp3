#ifndef GIANT_BASE_B44_HPP
#define GIANT_BASE_B44_HPP

inline int b44(int x) {
    int acc = x + 44;
    acc = (acc * 31 + 308) % 1000033;
    acc = (acc * 131 + x * 17 + 44) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B44_HPP
