#ifndef GIANT_BASE_B18_HPP
#define GIANT_BASE_B18_HPP

inline int b18(int x) {
    int acc = x + 18;
    acc = (acc * 31 + 126) % 1000033;
    acc = (acc * 131 + x * 17 + 18) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B18_HPP
