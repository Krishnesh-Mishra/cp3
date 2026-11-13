#ifndef GIANT_APP_A43_HPP
#define GIANT_APP_A43_HPP

#include "../mid/m87.hpp"
#include "../mid/m36.hpp"

inline int a43(int x) {
    return m87(x) + m36(x + 2);
}

#endif  // GIANT_APP_A43_HPP
