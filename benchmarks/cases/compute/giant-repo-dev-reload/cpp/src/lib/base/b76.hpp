#ifndef GIANT_BASE_B76_HPP
#define GIANT_BASE_B76_HPP

inline int b76(int x) {
    int acc = x + 76;
    acc = (acc * 31 + 532) % 1000033;
    acc = (acc * 131 + x * 17 + 76) % 1000199;
    return acc;
}

#endif  // GIANT_BASE_B76_HPP
