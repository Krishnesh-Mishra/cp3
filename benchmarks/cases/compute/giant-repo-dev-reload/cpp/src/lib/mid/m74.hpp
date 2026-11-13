#ifndef GIANT_MID_M74_HPP
#define GIANT_MID_M74_HPP

#include "../base/b94.hpp"
#include "../base/b6.hpp"

inline int m74(int x) {
    return b94(x) + b6(x + 1);
}

#endif  // GIANT_MID_M74_HPP
