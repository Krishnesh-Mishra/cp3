import { b18 } from "../base/b18.mjs";
import { b58 } from "../base/b58.mjs";
export function m6(x) {
    return b18(x) + b58(x + 1);
}
