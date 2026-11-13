#ifndef GIANT_APP_A17_HPP
#define GIANT_APP_A17_HPP

#include "../mid/m85.hpp"
#include "../mid/m34.hpp"

inline int a17(int x) {
    return m85(x) + m34(x + 2);
}

#endif  // GIANT_APP_A17_HPP
