#ifndef GIANT_BASE_B126_HPP
#define GIANT_BASE_B126_HPP

inline int b126(int x) {
    int acc = x + 126;
    acc = (acc * 31 + 882) % 1000033;
    acc = (acc * 131 + x * 17 + 126) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B126_HPP
