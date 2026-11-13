#ifndef GIANT_MID_M29_HPP
#define GIANT_MID_M29_HPP

#include "../base/b87.hpp"
#include "../base/b127.hpp"

inline int m29(int x) {
    return b87(x) + b127(x + 1);
}

#endif  // GIANT_MID_M29_HPP
