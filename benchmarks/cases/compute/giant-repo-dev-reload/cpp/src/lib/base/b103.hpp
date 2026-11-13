#ifndef GIANT_BASE_B103_HPP
#define GIANT_BASE_B103_HPP

inline int b103(int x) {
    int acc = x + 103;
    acc = (acc * 31 + 721) % 1000033;
    acc = (acc * 131 + x * 17 + 103) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B103_HPP
