#ifndef GIANT_BASE_B98_HPP
#define GIANT_BASE_B98_HPP

inline int b98(int x) {
    int acc = x + 98;
    acc = (acc * 31 + 686) % 1000033;
    acc = (acc * 131 + x * 17 + 98) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B98_HPP
