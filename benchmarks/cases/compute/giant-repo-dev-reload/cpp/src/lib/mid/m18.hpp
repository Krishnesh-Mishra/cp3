#ifndef GIANT_MID_M18_HPP
#define GIANT_MID_M18_HPP

#include "../base/b54.hpp"
#include "../base/b94.hpp"

inline int m18(int x) {
    return b54(x) + b94(x + 1);
}

#endif  // GIANT_MID_M18_HPP
