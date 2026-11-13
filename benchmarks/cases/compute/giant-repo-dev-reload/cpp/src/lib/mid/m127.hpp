#ifndef GIANT_MID_M127_HPP
#define GIANT_MID_M127_HPP

#include "../base/b125.hpp"
#include "../base/b37.hpp"

inline int m127(int x) {
    return b125(x) + b37(x + 1);
}

#endif  // GIANT_MID_M127_HPP
