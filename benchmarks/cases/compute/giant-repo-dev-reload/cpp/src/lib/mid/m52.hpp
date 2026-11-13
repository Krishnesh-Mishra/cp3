#ifndef GIANT_MID_M52_HPP
#define GIANT_MID_M52_HPP

#include "../base/b28.hpp"
#include "../base/b68.hpp"

inline int m52(int x) {
    return b28(x) + b68(x + 1);
}

#endif  // GIANT_MID_M52_HPP
