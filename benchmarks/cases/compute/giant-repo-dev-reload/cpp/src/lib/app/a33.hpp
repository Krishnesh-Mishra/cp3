#ifndef GIANT_APP_A33_HPP
#define GIANT_APP_A33_HPP

#include "../mid/m37.hpp"
#include "../mid/m114.hpp"

inline int a33(int x) {
    return m37(x) + m114(x + 2);
}

#endif  // GIANT_APP_A33_HPP
