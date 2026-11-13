#ifndef GIANT_MID_M99_HPP
#define GIANT_MID_M99_HPP

#include "../base/b41.hpp"
#include "../base/b81.hpp"

inline int m99(int x) {
    return b41(x) + b81(x + 1);
}

#endif  // GIANT_MID_M99_HPP
