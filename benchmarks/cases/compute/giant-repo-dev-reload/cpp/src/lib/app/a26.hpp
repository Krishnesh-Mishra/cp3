#ifndef GIANT_APP_A26_HPP
#define GIANT_APP_A26_HPP

#include "../mid/m2.hpp"
#include "../mid/m79.hpp"

inline int a26(int x) {
    return m2(x) + m79(x + 2);
}

#endif  // GIANT_APP_A26_HPP
