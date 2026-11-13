#ifndef GIANT_APP_A60_HPP
#define GIANT_APP_A60_HPP

#include "../mid/m44.hpp"
#include "../mid/m121.hpp"

inline int a60(int x) {
    return m44(x) + m121(x + 2);
}

#endif  // GIANT_APP_A60_HPP
