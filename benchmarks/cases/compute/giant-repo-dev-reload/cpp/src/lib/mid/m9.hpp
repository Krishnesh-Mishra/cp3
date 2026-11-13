#ifndef GIANT_MID_M9_HPP
#define GIANT_MID_M9_HPP

#include "../base/b27.hpp"
#include "../base/b67.hpp"

inline int m9(int x) {
    return b27(x) + b67(x + 1);
}

#endif  // GIANT_MID_M9_HPP
