export function b63(x) {
    let acc = x + 63;
    acc = (acc * 31 + 441) % 1000033;
    acc = (acc * 131 + x * 17 + 63) % 1000199;
    return acc;
}
