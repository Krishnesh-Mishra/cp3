#ifndef GIANT_BASE_B82_HPP
#define GIANT_BASE_B82_HPP

inline int b82(int x) {
    int acc = x + 82;
    acc = (acc * 31 + 574) % 1000033;
    acc = (acc * 131 + x * 17 + 82) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B82_HPP
