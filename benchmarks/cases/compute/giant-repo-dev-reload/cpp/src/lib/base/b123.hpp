#ifndef GIANT_BASE_B123_HPP
#define GIANT_BASE_B123_HPP

inline int b123(int x) {
    int acc = x + 123;
    acc = (acc * 31 + 861) % 1000033;
    acc = (acc * 131 + x * 17 + 123) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B123_HPP
