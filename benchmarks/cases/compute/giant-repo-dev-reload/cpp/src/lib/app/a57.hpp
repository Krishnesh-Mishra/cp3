#ifndef GIANT_APP_A57_HPP
#define GIANT_APP_A57_HPP

#include "../mid/m29.hpp"
#include "../mid/m106.hpp"

inline int a57(int x) {
    return m29(x) + m106(x + 2);
}

#endif  // GIANT_APP_A57_HPP
