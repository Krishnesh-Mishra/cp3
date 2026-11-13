package base

func B74(x int) int {
    acc := x + 74
    acc = (acc * 31 + 518) % 1000033
    acc = (acc * 131 + x * 17 + 74) % 1000199
    return acc
}

