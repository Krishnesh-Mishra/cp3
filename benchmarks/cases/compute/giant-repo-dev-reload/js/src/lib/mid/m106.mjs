import { b62 } from "../base/b62.mjs";
import { b102 } from "../base/b102.mjs";
export function m106(x) {
    return b62(x) + b102(x + 1);
}
