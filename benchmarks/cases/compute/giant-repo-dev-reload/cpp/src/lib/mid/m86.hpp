#ifndef GIANT_MID_M86_HPP
#define GIANT_MID_M86_HPP

#include "../base/b2.hpp"
#include "../base/b42.hpp"

inline int m86(int x) {
    return b2(x) + b42(x + 1);
}

#endif  // GIANT_MID_M86_HPP
