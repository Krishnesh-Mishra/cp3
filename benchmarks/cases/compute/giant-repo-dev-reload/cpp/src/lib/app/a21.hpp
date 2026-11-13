#ifndef GIANT_APP_A21_HPP
#define GIANT_APP_A21_HPP

#include "../mid/m105.hpp"
#include "../mid/m54.hpp"

inline int a21(int x) {
    return m105(x) + m54(x + 2);
}

#endif  // GIANT_APP_A21_HPP
