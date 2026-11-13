import { m44 } from "../mid/m44.mjs";
import { m121 } from "../mid/m121.mjs";
export function a60(x) {
    return m44(x) + m121(x + 2);
}
