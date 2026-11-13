#ifndef GIANT_APP_A4_HPP
#define GIANT_APP_A4_HPP

#include "../mid/m20.hpp"
#include "../mid/m97.hpp"

inline int a4(int x) {
    return m20(x) + m97(x + 2);
}

#endif  // GIANT_APP_A4_HPP
