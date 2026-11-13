#ifndef GIANT_BASE_B108_HPP
#define GIANT_BASE_B108_HPP

inline int b108(int x) {
    int acc = x + 108;
    acc = (acc * 31 + 756) % 1000033;
    acc = (acc * 131 + x * 17 + 108) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B108_HPP
