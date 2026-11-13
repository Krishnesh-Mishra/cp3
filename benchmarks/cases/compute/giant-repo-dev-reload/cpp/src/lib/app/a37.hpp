#ifndef GIANT_APP_A37_HPP
#define GIANT_APP_A37_HPP

#include "../mid/m57.hpp"
#include "../mid/m6.hpp"

inline int a37(int x) {
    return m57(x) + m6(x + 2);
}

#endif  // GIANT_APP_A37_HPP
