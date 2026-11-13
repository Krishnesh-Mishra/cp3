#ifndef GIANT_MID_M108_HPP
#define GIANT_MID_M108_HPP

#include "../base/b68.hpp"
#include "../base/b108.hpp"

inline int m108(int x) {
    return b68(x) + b108(x + 1);
}

#endif  // GIANT_MID_M108_HPP
