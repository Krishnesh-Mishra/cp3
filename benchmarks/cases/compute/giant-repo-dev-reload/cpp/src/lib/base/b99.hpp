#ifndef GIANT_BASE_B99_HPP
#define GIANT_BASE_B99_HPP

inline int b99(int x) {
    int acc = x + 99;
    acc = (acc * 31 + 693) % 1000033;
    acc = (acc * 131 + x * 17 + 99) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B99_HPP
