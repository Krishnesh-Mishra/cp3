#ifndef GIANT_BASE_B48_HPP
#define GIANT_BASE_B48_HPP

inline int b48(int x) {
    int acc = x + 48;
    acc = (acc * 31 + 336) % 1000033;
    acc = (acc * 131 + x * 17 + 48) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B48_HPP
