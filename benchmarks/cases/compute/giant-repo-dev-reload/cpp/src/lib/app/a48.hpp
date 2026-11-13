#ifndef GIANT_APP_A48_HPP
#define GIANT_APP_A48_HPP

#include "../mid/m112.hpp"
#include "../mid/m61.hpp"

inline int a48(int x) {
    return m112(x) + m61(x + 2);
}

#endif  // GIANT_APP_A48_HPP
