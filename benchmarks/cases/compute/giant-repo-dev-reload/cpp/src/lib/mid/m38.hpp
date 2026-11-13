#ifndef GIANT_MID_M38_HPP
#define GIANT_MID_M38_HPP

#include "../base/b114.hpp"
#include "../base/b26.hpp"

inline int m38(int x) {
    return b114(x) + b26(x + 1);
}

#endif  // GIANT_MID_M38_HPP
