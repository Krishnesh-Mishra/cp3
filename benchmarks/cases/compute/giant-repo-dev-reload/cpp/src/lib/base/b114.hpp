#ifndef GIANT_BASE_B114_HPP
#define GIANT_BASE_B114_HPP

inline int b114(int x) {
    int acc = x + 114;
    acc = (acc * 31 + 798) % 1000033;
    acc = (acc * 131 + x * 17 + 114) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B114_HPP
