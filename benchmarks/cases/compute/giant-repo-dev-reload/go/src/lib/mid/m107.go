package mid

import "giant/src/lib/base"

func M107(x int) int {
    return base.B65(x) + base.B105(x + 1)
}

