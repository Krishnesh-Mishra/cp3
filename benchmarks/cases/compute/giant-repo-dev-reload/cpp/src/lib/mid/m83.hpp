#ifndef GIANT_MID_M83_HPP
#define GIANT_MID_M83_HPP

#include "../base/b121.hpp"
#include "../base/b33.hpp"

inline int m83(int x) {
    return b121(x) + b33(x + 1);
}

#endif  // GIANT_MID_M83_HPP
