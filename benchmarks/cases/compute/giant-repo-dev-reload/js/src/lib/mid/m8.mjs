import { b24 } from "../base/b24.mjs";
import { b64 } from "../base/b64.mjs";
export function m8(x) {
    return b24(x) + b64(x + 1);
}
