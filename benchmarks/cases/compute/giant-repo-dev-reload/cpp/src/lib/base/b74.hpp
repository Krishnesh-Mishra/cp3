#ifndef GIANT_BASE_B74_HPP
#define GIANT_BASE_B74_HPP

inline int b74(int x) {
    int acc = x + 74;
    acc = (acc * 31 + 518) % 1000033;
    acc = (acc * 131 + x * 17 + 74) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B74_HPP
