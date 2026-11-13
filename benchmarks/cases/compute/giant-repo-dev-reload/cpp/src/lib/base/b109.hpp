#ifndef GIANT_BASE_B109_HPP
#define GIANT_BASE_B109_HPP

inline int b109(int x) {
    int acc = x + 109;
    acc = (acc * 31 + 763) % 1000033;
    acc = (acc * 131 + x * 17 + 109) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B109_HPP
