#ifndef GIANT_MID_M53_HPP
#define GIANT_MID_M53_HPP

#include "../base/b31.hpp"
#include "../base/b71.hpp"

inline int m53(int x) {
    return b31(x) + b71(x + 1);
}

#endif  // GIANT_MID_M53_HPP
