#ifndef GIANT_MID_M66_HPP
#define GIANT_MID_M66_HPP

#include "../base/b70.hpp"
#include "../base/b110.hpp"

inline int m66(int x) {
    return b70(x) + b110(x + 1);
}

#endif  // GIANT_MID_M66_HPP
