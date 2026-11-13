import { b45 } from "../base/b45.mjs";
import { b85 } from "../base/b85.mjs";
export function m15(x) {
    return b45(x) + b85(x + 1);
}
