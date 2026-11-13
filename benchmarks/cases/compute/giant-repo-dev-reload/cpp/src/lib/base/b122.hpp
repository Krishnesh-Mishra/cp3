#ifndef GIANT_BASE_B122_HPP
#define GIANT_BASE_B122_HPP

inline int b122(int x) {
    int acc = x + 122;
    acc = (acc * 31 + 854) % 1000033;
    acc = (acc * 131 + x * 17 + 122) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B122_HPP
