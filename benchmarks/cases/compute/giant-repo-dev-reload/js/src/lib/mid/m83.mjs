import { b121 } from "../base/b121.mjs";
import { b33 } from "../base/b33.mjs";
export function m83(x) {
    return b121(x) + b33(x + 1);
}
