#ifndef GIANT_BASE_B39_HPP
#define GIANT_BASE_B39_HPP

inline int b39(int x) {
    int acc = x + 39;
    acc = (acc * 31 + 273) % 1000033;
    acc = (acc * 131 + x * 17 + 39) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B39_HPP
