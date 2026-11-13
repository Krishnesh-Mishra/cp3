#ifndef GIANT_BASE_B45_HPP
#define GIANT_BASE_B45_HPP

inline int b45(int x) {
    int acc = x + 45;
    acc = (acc * 31 + 315) % 1000033;
    acc = (acc * 131 + x * 17 + 45) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B45_HPP
