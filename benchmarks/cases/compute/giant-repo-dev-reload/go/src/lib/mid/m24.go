package mid

import "giant/src/lib/base"

func M24(x int) int {
    return base.B72(x) + base.B112(x + 1)
}

