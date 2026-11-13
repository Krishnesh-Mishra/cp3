#ifndef GIANT_BASE_B125_HPP
#define GIANT_BASE_B125_HPP

inline int b125(int x) {
    int acc = x + 125;
    acc = (acc * 31 + 875) % 1000033;
    acc = (acc * 131 + x * 17 + 125) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B125_HPP
