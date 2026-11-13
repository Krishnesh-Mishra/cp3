#ifndef GIANT_BASE_B17_HPP
#define GIANT_BASE_B17_HPP

inline int b17(int x) {
    int acc = x + 17;
    acc = (acc * 31 + 119) % 1000033;
    acc = (acc * 131 + x * 17 + 17) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B17_HPP
