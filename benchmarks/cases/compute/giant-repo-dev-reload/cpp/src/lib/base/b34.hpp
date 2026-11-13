#ifndef GIANT_BASE_B34_HPP
#define GIANT_BASE_B34_HPP

inline int b34(int x) {
    int acc = x + 34;
    acc = (acc * 31 + 238) % 1000033;
    acc = (acc * 131 + x * 17 + 34) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B34_HPP
