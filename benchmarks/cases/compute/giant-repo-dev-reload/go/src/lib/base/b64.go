package base

func B64(x int) int {
    acc := x + 64
    acc = (acc * 31 + 448) % 1000033
    acc = (acc * 131 + x * 17 + 64) % 1000199
    return acc
}

