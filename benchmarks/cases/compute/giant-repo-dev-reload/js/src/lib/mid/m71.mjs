import { b85 } from "../base/b85.mjs";
import { b125 } from "../base/b125.mjs";
export function m71(x) {
    return b85(x) + b125(x + 1);
}
