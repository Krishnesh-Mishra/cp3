#ifndef GIANT_BASE_B89_HPP
#define GIANT_BASE_B89_HPP

inline int b89(int x) {
    int acc = x + 89;
    acc = (acc * 31 + 623) % 1000033;
    acc = (acc * 131 + x * 17 + 89) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B89_HPP
