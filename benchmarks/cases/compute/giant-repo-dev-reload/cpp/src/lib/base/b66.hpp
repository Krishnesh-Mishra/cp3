#ifndef GIANT_BASE_B66_HPP
#define GIANT_BASE_B66_HPP

inline int b66(int x) {
    int acc = x + 66;
    acc = (acc * 31 + 462) % 1000033;
    acc = (acc * 131 + x * 17 + 66) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B66_HPP
