#ifndef GIANT_MID_M85_HPP
#define GIANT_MID_M85_HPP

#include "../base/b127.hpp"
#include "../base/b39.hpp"

inline int m85(int x) {
    return b127(x) + b39(x + 1);
}

#endif  // GIANT_MID_M85_HPP
