#ifndef GIANT_APP_A20_HPP
#define GIANT_APP_A20_HPP

#include "../mid/m100.hpp"
#include "../mid/m49.hpp"

inline int a20(int x) {
    return m100(x) + m49(x + 2);
}

#endif  // GIANT_APP_A20_HPP
