package base

func B38(x int) int {
    acc := x + 38
    acc = (acc * 31 + 266) % 1000033
    acc = (acc * 131 + x * 17 + 38) % 1000199
    return acc
}

