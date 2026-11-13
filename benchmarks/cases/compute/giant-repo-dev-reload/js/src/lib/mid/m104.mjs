import { b56 } from "../base/b56.mjs";
import { b96 } from "../base/b96.mjs";
export function m104(x) {
    return b56(x) + b96(x + 1);
}
