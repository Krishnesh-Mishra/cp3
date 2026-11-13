#ifndef GIANT_BASE_B75_HPP
#define GIANT_BASE_B75_HPP

inline int b75(int x) {
    int acc = x + 75;
    acc = (acc * 31 + 525) % 1000033;
    acc = (acc * 131 + x * 17 + 75) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B75_HPP
