#ifndef GIANT_BASE_B64_HPP
#define GIANT_BASE_B64_HPP

inline int b64(int x) {
    int acc = x + 64;
    acc = (acc * 31 + 448) % 1000033;
    acc = (acc * 131 + x * 17 + 64) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B64_HPP
