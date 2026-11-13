#ifndef GIANT_APP_A45_HPP
#define GIANT_APP_A45_HPP

#include "../mid/m97.hpp"
#include "../mid/m46.hpp"

inline int a45(int x) {
    return m97(x) + m46(x + 2);
}

#endif  // GIANT_APP_A45_HPP
