package base

func B15(x int) int {
    acc := x + 15
    acc = (acc * 31 + 105) % 1000033
    acc = (acc * 131 + x * 17 + 15) % 1000199
    return acc
}

