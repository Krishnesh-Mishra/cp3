#ifndef GIANT_APP_A40_HPP
#define GIANT_APP_A40_HPP

#include "../mid/m72.hpp"
#include "../mid/m21.hpp"

inline int a40(int x) {
    return m72(x) + m21(x + 2);
}

#endif  // GIANT_APP_A40_HPP
