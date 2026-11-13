#ifndef GIANT_BASE_B96_HPP
#define GIANT_BASE_B96_HPP

inline int b96(int x) {
    int acc = x + 96;
    acc = (acc * 31 + 672) % 1000033;
    acc = (acc * 131 + x * 17 + 96) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B96_HPP
