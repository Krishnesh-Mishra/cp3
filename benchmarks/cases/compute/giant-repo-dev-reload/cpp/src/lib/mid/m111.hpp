#ifndef GIANT_MID_M111_HPP
#define GIANT_MID_M111_HPP

#include "../base/b77.hpp"
#include "../base/b117.hpp"

inline int m111(int x) {
    return b77(x) + b117(x + 1);
}

#endif  // GIANT_MID_M111_HPP
