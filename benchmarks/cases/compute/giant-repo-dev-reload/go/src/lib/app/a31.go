package app

import "giant/src/lib/mid"

func A31(x int) int {
    return mid.M27(x) + mid.M104(x + 2)
}

