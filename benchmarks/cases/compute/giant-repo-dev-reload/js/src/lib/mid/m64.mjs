import { b64 } from "../base/b64.mjs";
import { b104 } from "../base/b104.mjs";
export function m64(x) {
    return b64(x) + b104(x + 1);
}
