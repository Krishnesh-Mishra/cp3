package app

import "giant/src/lib/mid"

func A29(x int) int {
    return mid.M17(x) + mid.M94(x + 2)
}

