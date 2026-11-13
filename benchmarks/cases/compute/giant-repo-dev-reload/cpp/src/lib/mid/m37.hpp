#ifndef GIANT_MID_M37_HPP
#define GIANT_MID_M37_HPP

#include "../base/b111.hpp"
#include "../base/b23.hpp"

inline int m37(int x) {
    return b111(x) + b23(x + 1);
}

#endif  // GIANT_MID_M37_HPP
