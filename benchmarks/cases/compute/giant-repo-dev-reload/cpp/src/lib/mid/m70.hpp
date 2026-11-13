#ifndef GIANT_MID_M70_HPP
#define GIANT_MID_M70_HPP

#include "../base/b82.hpp"
#include "../base/b122.hpp"

inline int m70(int x) {
    return b82(x) + b122(x + 1);
}

#endif  // GIANT_MID_M70_HPP
