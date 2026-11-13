#ifndef GIANT_BASE_B119_HPP
#define GIANT_BASE_B119_HPP

inline int b119(int x) {
    int acc = x + 119;
    acc = (acc * 31 + 833) % 1000033;
    acc = (acc * 131 + x * 17 + 119) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B119_HPP
