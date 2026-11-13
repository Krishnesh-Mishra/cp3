#ifndef GIANT_APP_A30_HPP
#define GIANT_APP_A30_HPP

#include "../mid/m22.hpp"
#include "../mid/m99.hpp"

inline int a30(int x) {
    return m22(x) + m99(x + 2);
}

#endif  // GIANT_APP_A30_HPP
