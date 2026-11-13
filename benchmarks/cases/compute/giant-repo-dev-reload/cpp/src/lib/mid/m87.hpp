#ifndef GIANT_MID_M87_HPP
#define GIANT_MID_M87_HPP

#include "../base/b5.hpp"
#include "../base/b45.hpp"

inline int m87(int x) {
    return b5(x) + b45(x + 1);
}

#endif  // GIANT_MID_M87_HPP
