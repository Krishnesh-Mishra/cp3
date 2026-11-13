#ifndef GIANT_BASE_B111_HPP
#define GIANT_BASE_B111_HPP

inline int b111(int x) {
    int acc = x + 111;
    acc = (acc * 31 + 777) % 1000033;
    acc = (acc * 131 + x * 17 + 111) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B111_HPP
