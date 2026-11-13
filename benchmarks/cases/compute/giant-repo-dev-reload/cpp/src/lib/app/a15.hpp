#ifndef GIANT_APP_A15_HPP
#define GIANT_APP_A15_HPP

#include "../mid/m75.hpp"
#include "../mid/m24.hpp"

inline int a15(int x) {
    return m75(x) + m24(x + 2);
}

#endif  // GIANT_APP_A15_HPP
