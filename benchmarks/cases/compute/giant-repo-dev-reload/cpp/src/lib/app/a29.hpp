#ifndef GIANT_APP_A29_HPP
#define GIANT_APP_A29_HPP

#include "../mid/m17.hpp"
#include "../mid/m94.hpp"

inline int a29(int x) {
    return m17(x) + m94(x + 2);
}

#endif  // GIANT_APP_A29_HPP
