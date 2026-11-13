#ifndef GIANT_BASE_B55_HPP
#define GIANT_BASE_B55_HPP

inline int b55(int x) {
    int acc = x + 55;
    acc = (acc * 31 + 385) % 1000033;
    acc = (acc * 131 + x * 17 + 55) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B55_HPP
