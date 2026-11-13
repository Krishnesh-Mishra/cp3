#ifndef GIANT_MID_M31_HPP
#define GIANT_MID_M31_HPP

#include "../base/b93.hpp"
#include "../base/b5.hpp"

inline int m31(int x) {
    return b93(x) + b5(x + 1);
}

#endif  // GIANT_MID_M31_HPP
