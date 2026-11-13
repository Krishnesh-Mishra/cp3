#ifndef GIANT_APP_A34_HPP
#define GIANT_APP_A34_HPP

#include "../mid/m42.hpp"
#include "../mid/m119.hpp"

inline int a34(int x) {
    return m42(x) + m119(x + 2);
}

#endif  // GIANT_APP_A34_HPP
