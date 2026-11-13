#ifndef GIANT_BASE_B27_HPP
#define GIANT_BASE_B27_HPP

inline int b27(int x) {
    int acc = x + 27;
    acc = (acc * 31 + 189) % 1000033;
    acc = (acc * 131 + x * 17 + 27) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B27_HPP
