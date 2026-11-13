package app

import "giant/src/lib/mid"

func A20(x int) int {
    return mid.M100(x) + mid.M49(x + 2)
}

