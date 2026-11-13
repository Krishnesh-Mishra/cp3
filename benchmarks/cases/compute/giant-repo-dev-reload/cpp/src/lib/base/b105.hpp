#ifndef GIANT_BASE_B105_HPP
#define GIANT_BASE_B105_HPP

inline int b105(int x) {
    int acc = x + 105;
    acc = (acc * 31 + 735) % 1000033;
    acc = (acc * 131 + x * 17 + 105) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B105_HPP
