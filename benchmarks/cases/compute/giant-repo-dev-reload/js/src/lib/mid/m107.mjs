import { b65 } from "../base/b65.mjs";
import { b105 } from "../base/b105.mjs";
export function m107(x) {
    return b65(x) + b105(x + 1);
}
