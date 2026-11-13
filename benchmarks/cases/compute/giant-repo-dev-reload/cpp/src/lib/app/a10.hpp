#ifndef GIANT_APP_A10_HPP
#define GIANT_APP_A10_HPP

#include "../mid/m50.hpp"
#include "../mid/m127.hpp"

inline int a10(int x) {
    return m50(x) + m127(x + 2);
}

#endif  // GIANT_APP_A10_HPP
