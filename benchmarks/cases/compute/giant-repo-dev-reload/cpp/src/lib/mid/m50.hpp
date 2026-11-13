#ifndef GIANT_MID_M50_HPP
#define GIANT_MID_M50_HPP

#include "../base/b22.hpp"
#include "../base/b62.hpp"

inline int m50(int x) {
    return b22(x) + b62(x + 1);
}

#endif  // GIANT_MID_M50_HPP
