#ifndef GIANT_BASE_B23_HPP
#define GIANT_BASE_B23_HPP

inline int b23(int x) {
    int acc = x + 23;
    acc = (acc * 31 + 161) % 1000033;
    acc = (acc * 131 + x * 17 + 23) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B23_HPP
