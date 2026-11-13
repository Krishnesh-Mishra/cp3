package base

func B26(x int) int {
    acc := x + 26
    acc = (acc * 31 + 182) % 1000033
    acc = (acc * 131 + x * 17 + 26) % 1000199
    return acc
}

