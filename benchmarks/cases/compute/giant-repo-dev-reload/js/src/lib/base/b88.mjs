export function b88(x) {
    let acc = x + 88;
    acc = (acc * 31 + 616) % 1000033;
    acc = (acc * 131 + x * 17 + 88) % 1000199;
    return acc;
}
