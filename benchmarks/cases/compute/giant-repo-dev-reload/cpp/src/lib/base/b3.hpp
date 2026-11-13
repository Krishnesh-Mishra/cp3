#ifndef GIANT_BASE_B3_HPP
#define GIANT_BASE_B3_HPP

inline int b3(int x) {
    int acc = x + 3;
    acc = (acc * 31 + 21) % 1000033;
    acc = (acc * 131 + x * 17 + 3) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B3_HPP
