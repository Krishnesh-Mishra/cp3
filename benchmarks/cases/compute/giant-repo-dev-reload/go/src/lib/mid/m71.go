package mid

import "giant/src/lib/base"

func M71(x int) int {
    return base.B85(x) + base.B125(x + 1)
}

