#ifndef GIANT_MID_M95_HPP
#define GIANT_MID_M95_HPP

#include "../base/b29.hpp"
#include "../base/b69.hpp"

inline int m95(int x) {
    return b29(x) + b69(x + 1);
}

#endif  // GIANT_MID_M95_HPP
