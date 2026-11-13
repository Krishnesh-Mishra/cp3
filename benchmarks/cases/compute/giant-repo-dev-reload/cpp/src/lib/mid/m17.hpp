#ifndef GIANT_MID_M17_HPP
#define GIANT_MID_M17_HPP

#include "../base/b51.hpp"
#include "../base/b91.hpp"

inline int m17(int x) {
    return b51(x) + b91(x + 1);
}

#endif  // GIANT_MID_M17_HPP
