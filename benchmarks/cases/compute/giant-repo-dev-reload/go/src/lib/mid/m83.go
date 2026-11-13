package mid

import "giant/src/lib/base"

func M83(x int) int {
    return base.B121(x) + base.B33(x + 1)
}

