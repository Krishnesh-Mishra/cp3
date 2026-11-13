#ifndef GIANT_MID_M78_HPP
#define GIANT_MID_M78_HPP

#include "../base/b106.hpp"
#include "../base/b18.hpp"

inline int m78(int x) {
    return b106(x) + b18(x + 1);
}

#endif  // GIANT_MID_M78_HPP
