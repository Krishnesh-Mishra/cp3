#ifndef GIANT_MID_M27_HPP
#define GIANT_MID_M27_HPP

#include "../base/b81.hpp"
#include "../base/b121.hpp"

inline int m27(int x) {
    return b81(x) + b121(x + 1);
}

#endif  // GIANT_MID_M27_HPP
