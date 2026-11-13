#ifndef GIANT_BASE_B37_HPP
#define GIANT_BASE_B37_HPP

inline int b37(int x) {
    int acc = x + 37;
    acc = (acc * 31 + 259) % 1000033;
    acc = (acc * 131 + x * 17 + 37) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B37_HPP
