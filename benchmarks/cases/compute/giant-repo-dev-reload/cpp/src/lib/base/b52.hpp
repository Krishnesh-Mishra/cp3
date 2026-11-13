#ifndef GIANT_BASE_B52_HPP
#define GIANT_BASE_B52_HPP

inline int b52(int x) {
    int acc = x + 52;
    acc = (acc * 31 + 364) % 1000033;
    acc = (acc * 131 + x * 17 + 52) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B52_HPP
