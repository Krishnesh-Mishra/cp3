#ifndef GIANT_APP_A13_HPP
#define GIANT_APP_A13_HPP

#include "../mid/m65.hpp"
#include "../mid/m14.hpp"

inline int a13(int x) {
    return m65(x) + m14(x + 2);
}

#endif  // GIANT_APP_A13_HPP
