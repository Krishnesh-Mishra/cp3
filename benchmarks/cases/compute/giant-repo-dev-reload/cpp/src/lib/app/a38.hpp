#ifndef GIANT_APP_A38_HPP
#define GIANT_APP_A38_HPP

#include "../mid/m62.hpp"
#include "../mid/m11.hpp"

inline int a38(int x) {
    return m62(x) + m11(x + 2);
}

#endif  // GIANT_APP_A38_HPP
