#ifndef GIANT_BASE_B77_HPP
#define GIANT_BASE_B77_HPP

inline int b77(int x) {
    int acc = x + 77;
    acc = (acc * 31 + 539) % 1000033;
    acc = (acc * 131 + x * 17 + 77) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B77_HPP
