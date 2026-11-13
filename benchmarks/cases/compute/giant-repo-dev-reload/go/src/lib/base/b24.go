package base

func B24(x int) int {
    acc := x + 24
    acc = (acc * 31 + 168) % 1000033
    acc = (acc * 131 + x * 17 + 24) % 1000199
    return acc
}

