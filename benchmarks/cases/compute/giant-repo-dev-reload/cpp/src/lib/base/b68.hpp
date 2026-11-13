#ifndef GIANT_BASE_B68_HPP
#define GIANT_BASE_B68_HPP

inline int b68(int x) {
    int acc = x + 68;
    acc = (acc * 31 + 476) % 1000033;
    acc = (acc * 131 + x * 17 + 68) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B68_HPP
