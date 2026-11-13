import { b38 } from "../base/b38.mjs";
import { b78 } from "../base/b78.mjs";
export function m98(x) {
    return b38(x) + b78(x + 1);
}
