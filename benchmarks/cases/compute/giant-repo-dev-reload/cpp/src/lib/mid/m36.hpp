#ifndef GIANT_MID_M36_HPP
#define GIANT_MID_M36_HPP

#include "../base/b108.hpp"
#include "../base/b20.hpp"

inline int m36(int x) {
    return b108(x) + b20(x + 1);
}

#endif  // GIANT_MID_M36_HPP
