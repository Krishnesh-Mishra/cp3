#ifndef GIANT_BASE_B95_HPP
#define GIANT_BASE_B95_HPP

inline int b95(int x) {
    int acc = x + 95;
    acc = (acc * 31 + 665) % 1000033;
    acc = (acc * 131 + x * 17 + 95) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B95_HPP
