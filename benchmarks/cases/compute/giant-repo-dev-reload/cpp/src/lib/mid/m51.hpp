#ifndef GIANT_MID_M51_HPP
#define GIANT_MID_M51_HPP

#include "../base/b25.hpp"
#include "../base/b65.hpp"

inline int m51(int x) {
    return b25(x) + b65(x + 1);
}

#endif  // GIANT_MID_M51_HPP
