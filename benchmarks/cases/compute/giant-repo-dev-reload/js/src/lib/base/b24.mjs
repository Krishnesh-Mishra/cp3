export function b24(x) {
    let acc = x + 24;
    acc = (acc * 31 + 168) % 1000033;
    acc = (acc * 131 + x * 17 + 24) % 1000199;
    return acc;
}
