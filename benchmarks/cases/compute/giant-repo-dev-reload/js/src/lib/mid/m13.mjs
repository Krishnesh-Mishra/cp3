import { b39 } from "../base/b39.mjs";
import { b79 } from "../base/b79.mjs";
export function m13(x) {
    return b39(x) + b79(x + 1);
}
