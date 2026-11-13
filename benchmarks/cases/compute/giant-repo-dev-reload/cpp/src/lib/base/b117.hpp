#ifndef GIANT_BASE_B117_HPP
#define GIANT_BASE_B117_HPP

inline int b117(int x) {
    int acc = x + 117;
    acc = (acc * 31 + 819) % 1000033;
    acc = (acc * 131 + x * 17 + 117) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B117_HPP
