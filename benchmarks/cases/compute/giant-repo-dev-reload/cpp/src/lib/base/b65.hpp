#ifndef GIANT_BASE_B65_HPP
#define GIANT_BASE_B65_HPP

inline int b65(int x) {
    int acc = x + 65;
    acc = (acc * 31 + 455) % 1000033;
    acc = (acc * 131 + x * 17 + 65) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B65_HPP
