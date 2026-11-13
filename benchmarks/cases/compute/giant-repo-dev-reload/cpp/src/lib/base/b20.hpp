#ifndef GIANT_BASE_B20_HPP
#define GIANT_BASE_B20_HPP

inline int b20(int x) {
    int acc = x + 20;
    acc = (acc * 31 + 140) % 1000033;
    acc = (acc * 131 + x * 17 + 20) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B20_HPP
