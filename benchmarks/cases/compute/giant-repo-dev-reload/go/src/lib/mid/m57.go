package mid

import "giant/src/lib/base"

func M57(x int) int {
    return base.B43(x) + base.B83(x + 1)
}

