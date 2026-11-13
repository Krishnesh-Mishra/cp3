#ifndef GIANT_APP_A46_HPP
#define GIANT_APP_A46_HPP

#include "../mid/m102.hpp"
#include "../mid/m51.hpp"

inline int a46(int x) {
    return m102(x) + m51(x + 2);
}

#endif  // GIANT_APP_A46_HPP
