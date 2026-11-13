#ifndef GIANT_BASE_B80_HPP
#define GIANT_BASE_B80_HPP

inline int b80(int x) {
    int acc = x + 80;
    acc = (acc * 31 + 560) % 1000033;
    acc = (acc * 131 + x * 17 + 80) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B80_HPP
