package base

func B65(x int) int {
    acc := x + 65
    acc = (acc * 31 + 455) % 1000033
    acc = (acc * 131 + x * 17 + 65) % 1000199
    return acc
}

