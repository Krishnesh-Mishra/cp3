#ifndef GIANT_MID_M106_HPP
#define GIANT_MID_M106_HPP

#include "../base/b62.hpp"
#include "../base/b102.hpp"

inline int m106(int x) {
    return b62(x) + b102(x + 1);
}

#endif  // GIANT_MID_M106_HPP
