#ifndef GIANT_MID_M41_HPP
#define GIANT_MID_M41_HPP

#include "../base/b123.hpp"
#include "../base/b35.hpp"

inline int m41(int x) {
    return b123(x) + b35(x + 1);
}

#endif  // GIANT_MID_M41_HPP
