#ifndef GIANT_APP_A1_HPP
#define GIANT_APP_A1_HPP

#include "../mid/m5.hpp"
#include "../mid/m82.hpp"

inline int a1(int x) {
    return m5(x) + m82(x + 2);
}

#endif  // GIANT_APP_A1_HPP
