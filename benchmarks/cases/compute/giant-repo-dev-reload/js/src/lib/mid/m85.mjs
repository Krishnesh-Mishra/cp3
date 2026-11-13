import { b127 } from "../base/b127.mjs";
import { b39 } from "../base/b39.mjs";
export function m85(x) {
    return b127(x) + b39(x + 1);
}
