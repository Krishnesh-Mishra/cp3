#ifndef GIANT_BASE_B0_HPP
#define GIANT_BASE_B0_HPP

inline int b0(int x) {
    int acc = x + 0;
    acc = (acc * 31 + 0) % 1000033;
    acc = (acc * 131 + x * 17 + 0) % 1000199;
    return acc;
}

inline const char* greet() { return "Hello, world!"; }

#endif  // GIANT_BASE_B0_HPP
