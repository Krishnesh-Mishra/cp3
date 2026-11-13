export function b95(x) {
    let acc = x + 95;
    acc = (acc * 31 + 665) % 1000033;
    acc = (acc * 131 + x * 17 + 95) % 1000199;
    return acc;
}
