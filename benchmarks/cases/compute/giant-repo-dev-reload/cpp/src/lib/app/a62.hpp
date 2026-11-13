#ifndef GIANT_APP_A62_HPP
#define GIANT_APP_A62_HPP

#include "../mid/m54.hpp"
#include "../mid/m3.hpp"

inline int a62(int x) {
    return m54(x) + m3(x + 2);
}

#endif  // GIANT_APP_A62_HPP
