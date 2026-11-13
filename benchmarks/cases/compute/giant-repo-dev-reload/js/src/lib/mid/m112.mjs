import { b80 } from "../base/b80.mjs";
import { b120 } from "../base/b120.mjs";
export function m112(x) {
    return b80(x) + b120(x + 1);
}
