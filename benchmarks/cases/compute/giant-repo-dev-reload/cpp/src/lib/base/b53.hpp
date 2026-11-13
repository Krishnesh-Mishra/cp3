#ifndef GIANT_BASE_B53_HPP
#define GIANT_BASE_B53_HPP

inline int b53(int x) {
    int acc = x + 53;
    acc = (acc * 31 + 371) % 1000033;
    acc = (acc * 131 + x * 17 + 53) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B53_HPP
