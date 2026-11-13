#ifndef GIANT_BASE_B33_HPP
#define GIANT_BASE_B33_HPP

inline int b33(int x) {
    int acc = x + 33;
    acc = (acc * 31 + 231) % 1000033;
    acc = (acc * 131 + x * 17 + 33) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B33_HPP
