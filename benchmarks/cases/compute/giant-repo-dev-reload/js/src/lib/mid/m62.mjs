import { b58 } from "../base/b58.mjs";
import { b98 } from "../base/b98.mjs";
export function m62(x) {
    return b58(x) + b98(x + 1);
}
