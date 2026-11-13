#ifndef GIANT_MID_M19_HPP
#define GIANT_MID_M19_HPP

#include "../base/b57.hpp"
#include "../base/b97.hpp"

inline int m19(int x) {
    return b57(x) + b97(x + 1);
}

#endif  // GIANT_MID_M19_HPP
