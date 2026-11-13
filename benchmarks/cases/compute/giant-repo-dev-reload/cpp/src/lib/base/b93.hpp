#ifndef GIANT_BASE_B93_HPP
#define GIANT_BASE_B93_HPP

inline int b93(int x) {
    int acc = x + 93;
    acc = (acc * 31 + 651) % 1000033;
    acc = (acc * 131 + x * 17 + 93) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B93_HPP
