#ifndef GIANT_BASE_B19_HPP
#define GIANT_BASE_B19_HPP

inline int b19(int x) {
    int acc = x + 19;
    acc = (acc * 31 + 133) % 1000033;
    acc = (acc * 131 + x * 17 + 19) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B19_HPP
