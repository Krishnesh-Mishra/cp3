#ifndef GIANT_BASE_B115_HPP
#define GIANT_BASE_B115_HPP

inline int b115(int x) {
    int acc = x + 115;
    acc = (acc * 31 + 805) % 1000033;
    acc = (acc * 131 + x * 17 + 115) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B115_HPP
