#ifndef GIANT_MID_M79_HPP
#define GIANT_MID_M79_HPP

#include "../base/b109.hpp"
#include "../base/b21.hpp"

inline int m79(int x) {
    return b109(x) + b21(x + 1);
}

#endif  // GIANT_MID_M79_HPP
