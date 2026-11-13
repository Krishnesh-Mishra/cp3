#ifndef GIANT_MID_M77_HPP
#define GIANT_MID_M77_HPP

#include "../base/b103.hpp"
#include "../base/b15.hpp"

inline int m77(int x) {
    return b103(x) + b15(x + 1);
}

#endif  // GIANT_MID_M77_HPP
