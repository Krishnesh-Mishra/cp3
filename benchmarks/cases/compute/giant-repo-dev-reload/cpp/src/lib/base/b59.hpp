#ifndef GIANT_BASE_B59_HPP
#define GIANT_BASE_B59_HPP

inline int b59(int x) {
    int acc = x + 59;
    acc = (acc * 31 + 413) % 1000033;
    acc = (acc * 131 + x * 17 + 59) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B59_HPP
