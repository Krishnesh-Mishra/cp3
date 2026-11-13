#ifndef GIANT_MID_M73_HPP
#define GIANT_MID_M73_HPP

#include "../base/b91.hpp"
#include "../base/b3.hpp"

inline int m73(int x) {
    return b91(x) + b3(x + 1);
}

#endif  // GIANT_MID_M73_HPP
