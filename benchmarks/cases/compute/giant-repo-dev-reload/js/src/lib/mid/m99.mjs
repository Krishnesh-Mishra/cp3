import { b41 } from "../base/b41.mjs";
import { b81 } from "../base/b81.mjs";
export function m99(x) {
    return b41(x) + b81(x + 1);
}
