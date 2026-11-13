#ifndef GIANT_MID_M64_HPP
#define GIANT_MID_M64_HPP

#include "../base/b64.hpp"
#include "../base/b104.hpp"

inline int m64(int x) {
    return b64(x) + b104(x + 1);
}

#endif  // GIANT_MID_M64_HPP
