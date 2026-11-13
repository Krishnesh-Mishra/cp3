#ifndef GIANT_BASE_B94_HPP
#define GIANT_BASE_B94_HPP

inline int b94(int x) {
    int acc = x + 94;
    acc = (acc * 31 + 658) % 1000033;
    acc = (acc * 131 + x * 17 + 94) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B94_HPP
