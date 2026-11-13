export function b87(x) {
    let acc = x + 87;
    acc = (acc * 31 + 609) % 1000033;
    acc = (acc * 131 + x * 17 + 87) % 1000199;
    return acc;
}
