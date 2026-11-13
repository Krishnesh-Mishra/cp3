#ifndef GIANT_APP_A50_HPP
#define GIANT_APP_A50_HPP

#include "../mid/m122.hpp"
#include "../mid/m71.hpp"

inline int a50(int x) {
    return m122(x) + m71(x + 2);
}

#endif  // GIANT_APP_A50_HPP
