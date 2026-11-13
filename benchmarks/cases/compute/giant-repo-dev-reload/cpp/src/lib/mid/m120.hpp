#ifndef GIANT_MID_M120_HPP
#define GIANT_MID_M120_HPP

#include "../base/b104.hpp"
#include "../base/b16.hpp"

inline int m120(int x) {
    return b104(x) + b16(x + 1);
}

#endif  // GIANT_MID_M120_HPP
