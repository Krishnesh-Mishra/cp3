package app

import "giant/src/lib/mid"

func A17(x int) int {
    return mid.M85(x) + mid.M34(x + 2)
}

