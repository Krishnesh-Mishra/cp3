#ifndef GIANT_APP_A24_HPP
#define GIANT_APP_A24_HPP

#include "../mid/m120.hpp"
#include "../mid/m69.hpp"

inline int a24(int x) {
    return m120(x) + m69(x + 2);
}

#endif  // GIANT_APP_A24_HPP
