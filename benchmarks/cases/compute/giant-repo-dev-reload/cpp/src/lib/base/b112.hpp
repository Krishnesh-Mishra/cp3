#ifndef GIANT_BASE_B112_HPP
#define GIANT_BASE_B112_HPP

inline int b112(int x) {
    int acc = x + 112;
    acc = (acc * 31 + 784) % 1000033;
    acc = (acc * 131 + x * 17 + 112) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B112_HPP
