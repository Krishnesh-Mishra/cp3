#ifndef GIANT_BASE_B41_HPP
#define GIANT_BASE_B41_HPP

inline int b41(int x) {
    int acc = x + 41;
    acc = (acc * 31 + 287) % 1000033;
    acc = (acc * 131 + x * 17 + 41) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B41_HPP
