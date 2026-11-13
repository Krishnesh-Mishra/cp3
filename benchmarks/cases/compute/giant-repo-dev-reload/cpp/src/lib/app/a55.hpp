#ifndef GIANT_APP_A55_HPP
#define GIANT_APP_A55_HPP

#include "../mid/m19.hpp"
#include "../mid/m96.hpp"

inline int a55(int x) {
    return m19(x) + m96(x + 2);
}

#endif  // GIANT_APP_A55_HPP
