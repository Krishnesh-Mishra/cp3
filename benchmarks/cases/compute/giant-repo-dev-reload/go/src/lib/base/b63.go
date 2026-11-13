package base

func B63(x int) int {
    acc := x + 63
    acc = (acc * 31 + 441) % 1000033
    acc = (acc * 131 + x * 17 + 63) % 1000199
    return acc
}

