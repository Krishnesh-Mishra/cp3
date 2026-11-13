package base

func B56(x int) int {
    acc := x + 56
    acc = (acc * 31 + 392) % 1000033
    acc = (acc * 131 + x * 17 + 56) % 1000199
    return acc
}

