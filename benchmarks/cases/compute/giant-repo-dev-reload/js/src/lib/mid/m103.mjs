import { b53 } from "../base/b53.mjs";
import { b93 } from "../base/b93.mjs";
export function m103(x) {
    return b53(x) + b93(x + 1);
}
