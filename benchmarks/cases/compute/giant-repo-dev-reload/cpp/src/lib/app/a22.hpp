#ifndef GIANT_APP_A22_HPP
#define GIANT_APP_A22_HPP

#include "../mid/m110.hpp"
#include "../mid/m59.hpp"

inline int a22(int x) {
    return m110(x) + m59(x + 2);
}

#endif  // GIANT_APP_A22_HPP
