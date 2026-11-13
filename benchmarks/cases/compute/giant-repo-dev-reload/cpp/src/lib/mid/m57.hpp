#ifndef GIANT_MID_M57_HPP
#define GIANT_MID_M57_HPP

#include "../base/b43.hpp"
#include "../base/b83.hpp"

inline int m57(int x) {
    return b43(x) + b83(x + 1);
}

#endif  // GIANT_MID_M57_HPP
