#ifndef GIANT_APP_A6_HPP
#define GIANT_APP_A6_HPP

#include "../mid/m30.hpp"
#include "../mid/m107.hpp"

inline int a6(int x) {
    return m30(x) + m107(x + 2);
}

#endif  // GIANT_APP_A6_HPP
