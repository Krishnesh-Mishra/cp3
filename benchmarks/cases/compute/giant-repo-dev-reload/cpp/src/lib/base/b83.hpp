#ifndef GIANT_BASE_B83_HPP
#define GIANT_BASE_B83_HPP

inline int b83(int x) {
    int acc = x + 83;
    acc = (acc * 31 + 581) % 1000033;
    acc = (acc * 131 + x * 17 + 83) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B83_HPP
