import { b16 } from "../base/b16.mjs";
import { b56 } from "../base/b56.mjs";
export function m48(x) {
    return b16(x) + b56(x + 1);
}
