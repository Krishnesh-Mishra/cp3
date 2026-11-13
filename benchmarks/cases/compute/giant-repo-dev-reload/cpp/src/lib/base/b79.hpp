#ifndef GIANT_BASE_B79_HPP
#define GIANT_BASE_B79_HPP

inline int b79(int x) {
    int acc = x + 79;
    acc = (acc * 31 + 553) % 1000033;
    acc = (acc * 131 + x * 17 + 79) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B79_HPP
