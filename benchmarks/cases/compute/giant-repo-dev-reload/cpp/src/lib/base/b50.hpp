#ifndef GIANT_BASE_B50_HPP
#define GIANT_BASE_B50_HPP

inline int b50(int x) {
    int acc = x + 50;
    acc = (acc * 31 + 350) % 1000033;
    acc = (acc * 131 + x * 17 + 50) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B50_HPP
