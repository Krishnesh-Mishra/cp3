#ifndef GIANT_APP_A56_HPP
#define GIANT_APP_A56_HPP

#include "../mid/m24.hpp"
#include "../mid/m101.hpp"

inline int a56(int x) {
    return m24(x) + m101(x + 2);
}

#endif  // GIANT_APP_A56_HPP
