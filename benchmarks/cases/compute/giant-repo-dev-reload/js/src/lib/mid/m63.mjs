import { b61 } from "../base/b61.mjs";
import { b101 } from "../base/b101.mjs";
export function m63(x) {
    return b61(x) + b101(x + 1);
}
