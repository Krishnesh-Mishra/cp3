package app

import "giant/src/lib/mid"

func A16(x int) int {
    return mid.M80(x) + mid.M29(x + 2)
}

