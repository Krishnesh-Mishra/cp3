package mid

import "giant/src/lib/base"

func M106(x int) int {
    return base.B62(x) + base.B102(x + 1)
}

