#ifndef GIANT_APP_A58_HPP
#define GIANT_APP_A58_HPP

#include "../mid/m34.hpp"
#include "../mid/m111.hpp"

inline int a58(int x) {
    return m34(x) + m111(x + 2);
}

#endif  // GIANT_APP_A58_HPP
