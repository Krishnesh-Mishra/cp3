#ifndef GIANT_BASE_B110_HPP
#define GIANT_BASE_B110_HPP

inline int b110(int x) {
    int acc = x + 110;
    acc = (acc * 31 + 770) % 1000033;
    acc = (acc * 131 + x * 17 + 110) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B110_HPP
