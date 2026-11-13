package base

func B14(x int) int {
    acc := x + 14
    acc = (acc * 31 + 98) % 1000033
    acc = (acc * 131 + x * 17 + 14) % 1000199
    return acc
}

