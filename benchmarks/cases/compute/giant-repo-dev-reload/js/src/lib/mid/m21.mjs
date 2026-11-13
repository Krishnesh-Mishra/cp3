import { b63 } from "../base/b63.mjs";
import { b103 } from "../base/b103.mjs";
export function m21(x) {
    return b63(x) + b103(x + 1);
}
