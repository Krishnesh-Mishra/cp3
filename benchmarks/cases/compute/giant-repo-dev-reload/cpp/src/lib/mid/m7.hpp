#ifndef GIANT_MID_M7_HPP
#define GIANT_MID_M7_HPP

#include "../base/b21.hpp"
#include "../base/b61.hpp"

inline int m7(int x) {
    return b21(x) + b61(x + 1);
}

#endif  // GIANT_MID_M7_HPP
