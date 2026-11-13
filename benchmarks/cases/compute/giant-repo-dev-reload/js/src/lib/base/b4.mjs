export function b4(x) {
    let acc = x + 4;
    acc = (acc * 31 + 28) % 1000033;
    acc = (acc * 131 + x * 17 + 4) % 1000199;
    return acc;
}
