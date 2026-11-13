#ifndef GIANT_MID_M46_HPP
#define GIANT_MID_M46_HPP

#include "../base/b10.hpp"
#include "../base/b50.hpp"

inline int m46(int x) {
    return b10(x) + b50(x + 1);
}

#endif  // GIANT_MID_M46_HPP
