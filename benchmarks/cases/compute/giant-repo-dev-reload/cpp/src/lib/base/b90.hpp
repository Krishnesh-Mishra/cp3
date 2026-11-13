#ifndef GIANT_BASE_B90_HPP
#define GIANT_BASE_B90_HPP

inline int b90(int x) {
    int acc = x + 90;
    acc = (acc * 31 + 630) % 1000033;
    acc = (acc * 131 + x * 17 + 90) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B90_HPP
