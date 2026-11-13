#ifndef GIANT_BASE_B69_HPP
#define GIANT_BASE_B69_HPP

inline int b69(int x) {
    int acc = x + 69;
    acc = (acc * 31 + 483) % 1000033;
    acc = (acc * 131 + x * 17 + 69) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B69_HPP
