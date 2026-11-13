package app

import "giant/src/lib/mid"

func A25(x int) int {
    return mid.M125(x) + mid.M74(x + 2)
}

