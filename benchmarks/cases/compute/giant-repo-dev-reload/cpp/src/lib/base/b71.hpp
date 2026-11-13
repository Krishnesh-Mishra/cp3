#ifndef GIANT_BASE_B71_HPP
#define GIANT_BASE_B71_HPP

inline int b71(int x) {
    int acc = x + 71;
    acc = (acc * 31 + 497) % 1000033;
    acc = (acc * 131 + x * 17 + 71) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B71_HPP
