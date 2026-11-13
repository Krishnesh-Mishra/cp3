package mid

import "giant/src/lib/base"

func M52(x int) int {
    return base.B28(x) + base.B68(x + 1)
}

