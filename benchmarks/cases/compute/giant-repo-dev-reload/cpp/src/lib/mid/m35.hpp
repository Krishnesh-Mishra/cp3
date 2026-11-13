#ifndef GIANT_MID_M35_HPP
#define GIANT_MID_M35_HPP

#include "../base/b105.hpp"
#include "../base/b17.hpp"

inline int m35(int x) {
    return b105(x) + b17(x + 1);
}

#endif  // GIANT_MID_M35_HPP
