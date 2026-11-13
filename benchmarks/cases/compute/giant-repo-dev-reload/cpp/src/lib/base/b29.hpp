#ifndef GIANT_BASE_B29_HPP
#define GIANT_BASE_B29_HPP

inline int b29(int x) {
    int acc = x + 29;
    acc = (acc * 31 + 203) % 1000033;
    acc = (acc * 131 + x * 17 + 29) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B29_HPP
