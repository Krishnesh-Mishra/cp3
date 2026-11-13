#ifndef GIANT_BASE_B97_HPP
#define GIANT_BASE_B97_HPP

inline int b97(int x) {
    int acc = x + 97;
    acc = (acc * 31 + 679) % 1000033;
    acc = (acc * 131 + x * 17 + 97) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B97_HPP
