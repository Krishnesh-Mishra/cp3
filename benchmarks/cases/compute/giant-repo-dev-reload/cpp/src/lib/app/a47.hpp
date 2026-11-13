#ifndef GIANT_APP_A47_HPP
#define GIANT_APP_A47_HPP

#include "../mid/m107.hpp"
#include "../mid/m56.hpp"

inline int a47(int x) {
    return m107(x) + m56(x + 2);
}

#endif  // GIANT_APP_A47_HPP
