export function b91(x) {
    let acc = x + 91;
    acc = (acc * 31 + 637) % 1000033;
    acc = (acc * 131 + x * 17 + 91) % 1000199;
    return acc;
}
