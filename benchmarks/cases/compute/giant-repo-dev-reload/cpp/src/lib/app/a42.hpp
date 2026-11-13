#ifndef GIANT_APP_A42_HPP
#define GIANT_APP_A42_HPP

#include "../mid/m82.hpp"
#include "../mid/m31.hpp"

inline int a42(int x) {
    return m82(x) + m31(x + 2);
}

#endif  // GIANT_APP_A42_HPP
