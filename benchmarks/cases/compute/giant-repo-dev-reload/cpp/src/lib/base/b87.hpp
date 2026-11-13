#ifndef GIANT_BASE_B87_HPP
#define GIANT_BASE_B87_HPP

inline int b87(int x) {
    int acc = x + 87;
    acc = (acc * 31 + 609) % 1000033;
    acc = (acc * 131 + x * 17 + 87) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B87_HPP
