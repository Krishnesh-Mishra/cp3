#ifndef GIANT_BASE_B86_HPP
#define GIANT_BASE_B86_HPP

inline int b86(int x) {
    int acc = x + 86;
    acc = (acc * 31 + 602) % 1000033;
    acc = (acc * 131 + x * 17 + 86) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B86_HPP
