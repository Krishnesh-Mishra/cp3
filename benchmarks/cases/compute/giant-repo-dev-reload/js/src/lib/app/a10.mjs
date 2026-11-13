import { m50 } from "../mid/m50.mjs";
import { m127 } from "../mid/m127.mjs";
export function a10(x) {
    return m50(x) + m127(x + 2);
}
