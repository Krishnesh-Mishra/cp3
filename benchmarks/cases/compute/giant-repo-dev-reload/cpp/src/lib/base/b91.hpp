#ifndef GIANT_BASE_B91_HPP
#define GIANT_BASE_B91_HPP

inline int b91(int x) {
    int acc = x + 91;
    acc = (acc * 31 + 637) % 1000033;
    acc = (acc * 131 + x * 17 + 91) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B91_HPP
