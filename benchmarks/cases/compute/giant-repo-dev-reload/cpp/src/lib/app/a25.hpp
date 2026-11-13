#ifndef GIANT_APP_A25_HPP
#define GIANT_APP_A25_HPP

#include "../mid/m125.hpp"
#include "../mid/m74.hpp"

inline int a25(int x) {
    return m125(x) + m74(x + 2);
}

#endif  // GIANT_APP_A25_HPP
