import { b84 } from "../base/b84.mjs";
import { b124 } from "../base/b124.mjs";
export function m28(x) {
    return b84(x) + b124(x + 1);
}
