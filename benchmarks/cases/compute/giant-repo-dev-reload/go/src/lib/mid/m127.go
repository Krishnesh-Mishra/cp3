package mid

import "giant/src/lib/base"

func M127(x int) int {
    return base.B125(x) + base.B37(x + 1)
}

