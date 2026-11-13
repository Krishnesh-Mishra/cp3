package mid

import "giant/src/lib/base"

func M25(x int) int {
    return base.B75(x) + base.B115(x + 1)
}

