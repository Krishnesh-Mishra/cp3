package mid

import "giant/src/lib/base"

func M99(x int) int {
    return base.B41(x) + base.B81(x + 1)
}

