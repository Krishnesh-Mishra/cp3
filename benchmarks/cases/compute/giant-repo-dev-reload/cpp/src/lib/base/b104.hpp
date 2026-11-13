#ifndef GIANT_BASE_B104_HPP
#define GIANT_BASE_B104_HPP

inline int b104(int x) {
    int acc = x + 104;
    acc = (acc * 31 + 728) % 1000033;
    acc = (acc * 131 + x * 17 + 104) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B104_HPP
