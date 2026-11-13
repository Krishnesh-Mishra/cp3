#ifndef GIANT_BASE_B58_HPP
#define GIANT_BASE_B58_HPP

inline int b58(int x) {
    int acc = x + 58;
    acc = (acc * 31 + 406) % 1000033;
    acc = (acc * 131 + x * 17 + 58) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B58_HPP
