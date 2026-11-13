use crate::base::b64::b64;
use crate::base::b104::b104;

pub fn m64(x: i64) -> i64 {
    b64(x) + b104(x + 1)
}

