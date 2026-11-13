#ifndef GIANT_MID_M8_HPP
#define GIANT_MID_M8_HPP

#include "../base/b24.hpp"
#include "../base/b64.hpp"

inline int m8(int x) {
    return b24(x) + b64(x + 1);
}

#endif  // GIANT_MID_M8_HPP
