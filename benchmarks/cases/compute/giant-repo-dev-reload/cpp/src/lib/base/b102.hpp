#ifndef GIANT_BASE_B102_HPP
#define GIANT_BASE_B102_HPP

inline int b102(int x) {
    int acc = x + 102;
    acc = (acc * 31 + 714) % 1000033;
    acc = (acc * 131 + x * 17 + 102) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B102_HPP
