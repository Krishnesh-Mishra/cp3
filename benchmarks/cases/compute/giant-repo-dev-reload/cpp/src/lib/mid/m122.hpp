#ifndef GIANT_MID_M122_HPP
#define GIANT_MID_M122_HPP

#include "../base/b110.hpp"
#include "../base/b22.hpp"

inline int m122(int x) {
    return b110(x) + b22(x + 1);
}

#endif  // GIANT_MID_M122_HPP
