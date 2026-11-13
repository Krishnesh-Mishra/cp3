#ifndef GIANT_MID_M126_HPP
#define GIANT_MID_M126_HPP

#include "../base/b122.hpp"
#include "../base/b34.hpp"

inline int m126(int x) {
    return b122(x) + b34(x + 1);
}

#endif  // GIANT_MID_M126_HPP
