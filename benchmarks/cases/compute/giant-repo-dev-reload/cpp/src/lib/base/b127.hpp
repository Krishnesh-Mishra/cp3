#ifndef GIANT_BASE_B127_HPP
#define GIANT_BASE_B127_HPP

inline int b127(int x) {
    int acc = x + 127;
    acc = (acc * 31 + 889) % 1000033;
    acc = (acc * 131 + x * 17 + 127) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B127_HPP
