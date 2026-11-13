#ifndef GIANT_BASE_B121_HPP
#define GIANT_BASE_B121_HPP

inline int b121(int x) {
    int acc = x + 121;
    acc = (acc * 31 + 847) % 1000033;
    acc = (acc * 131 + x * 17 + 121) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B121_HPP
