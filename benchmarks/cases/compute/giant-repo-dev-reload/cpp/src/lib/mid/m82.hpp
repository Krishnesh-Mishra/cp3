#ifndef GIANT_MID_M82_HPP
#define GIANT_MID_M82_HPP

#include "../base/b118.hpp"
#include "../base/b30.hpp"

inline int m82(int x) {
    return b118(x) + b30(x + 1);
}

#endif  // GIANT_MID_M82_HPP
