#ifndef GIANT_BASE_B57_HPP
#define GIANT_BASE_B57_HPP

inline int b57(int x) {
    int acc = x + 57;
    acc = (acc * 31 + 399) % 1000033;
    acc = (acc * 131 + x * 17 + 57) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B57_HPP
