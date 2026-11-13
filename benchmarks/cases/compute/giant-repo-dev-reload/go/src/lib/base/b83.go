package base

func B83(x int) int {
    acc := x + 83
    acc = (acc * 31 + 581) % 1000033
    acc = (acc * 131 + x * 17 + 83) % 1000199
    return acc
}

