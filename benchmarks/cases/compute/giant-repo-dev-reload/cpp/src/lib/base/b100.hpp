#ifndef GIANT_BASE_B100_HPP
#define GIANT_BASE_B100_HPP

inline int b100(int x) {
    int acc = x + 100;
    acc = (acc * 31 + 700) % 1000033;
    acc = (acc * 131 + x * 17 + 100) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B100_HPP
