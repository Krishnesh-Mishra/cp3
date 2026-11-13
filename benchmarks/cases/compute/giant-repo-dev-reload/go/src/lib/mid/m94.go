package mid

import "giant/src/lib/base"

func M94(x int) int {
    return base.B26(x) + base.B66(x + 1)
}

