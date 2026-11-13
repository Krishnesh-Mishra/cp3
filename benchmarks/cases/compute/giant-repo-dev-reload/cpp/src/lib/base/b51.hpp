#ifndef GIANT_BASE_B51_HPP
#define GIANT_BASE_B51_HPP

inline int b51(int x) {
    int acc = x + 51;
    acc = (acc * 31 + 357) % 1000033;
    acc = (acc * 131 + x * 17 + 51) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B51_HPP
