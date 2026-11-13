#ifndef GIANT_BASE_B107_HPP
#define GIANT_BASE_B107_HPP

inline int b107(int x) {
    int acc = x + 107;
    acc = (acc * 31 + 749) % 1000033;
    acc = (acc * 131 + x * 17 + 107) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B107_HPP
