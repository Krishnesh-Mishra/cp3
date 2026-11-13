#ifndef GIANT_BASE_B26_HPP
#define GIANT_BASE_B26_HPP

inline int b26(int x) {
    int acc = x + 26;
    acc = (acc * 31 + 182) % 1000033;
    acc = (acc * 131 + x * 17 + 26) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B26_HPP
