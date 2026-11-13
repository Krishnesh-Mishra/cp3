#ifndef GIANT_MID_M59_HPP
#define GIANT_MID_M59_HPP

#include "../base/b49.hpp"
#include "../base/b89.hpp"

inline int m59(int x) {
    return b49(x) + b89(x + 1);
}

#endif  // GIANT_MID_M59_HPP
