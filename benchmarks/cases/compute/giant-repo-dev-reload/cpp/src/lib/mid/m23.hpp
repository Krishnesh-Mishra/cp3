#ifndef GIANT_MID_M23_HPP
#define GIANT_MID_M23_HPP

#include "../base/b69.hpp"
#include "../base/b109.hpp"

inline int m23(int x) {
    return b69(x) + b109(x + 1);
}

#endif  // GIANT_MID_M23_HPP
