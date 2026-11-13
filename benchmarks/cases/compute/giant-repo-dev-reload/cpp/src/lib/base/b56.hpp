#ifndef GIANT_BASE_B56_HPP
#define GIANT_BASE_B56_HPP

inline int b56(int x) {
    int acc = x + 56;
    acc = (acc * 31 + 392) % 1000033;
    acc = (acc * 131 + x * 17 + 56) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B56_HPP
