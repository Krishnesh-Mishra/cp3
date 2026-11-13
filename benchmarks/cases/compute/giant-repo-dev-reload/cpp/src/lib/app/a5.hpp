#ifndef GIANT_APP_A5_HPP
#define GIANT_APP_A5_HPP

#include "../mid/m25.hpp"
#include "../mid/m102.hpp"

inline int a5(int x) {
    return m25(x) + m102(x + 2);
}

#endif  // GIANT_APP_A5_HPP
