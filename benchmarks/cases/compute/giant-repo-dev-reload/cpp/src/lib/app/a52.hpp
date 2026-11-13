#ifndef GIANT_APP_A52_HPP
#define GIANT_APP_A52_HPP

#include "../mid/m4.hpp"
#include "../mid/m81.hpp"

inline int a52(int x) {
    return m4(x) + m81(x + 2);
}

#endif  // GIANT_APP_A52_HPP
