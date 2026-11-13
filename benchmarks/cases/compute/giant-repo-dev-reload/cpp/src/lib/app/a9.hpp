#ifndef GIANT_APP_A9_HPP
#define GIANT_APP_A9_HPP

#include "../mid/m45.hpp"
#include "../mid/m122.hpp"

inline int a9(int x) {
    return m45(x) + m122(x + 2);
}

#endif  // GIANT_APP_A9_HPP
