use crate::base::b24::b24;
use crate::base::b64::b64;

pub fn m8(x: i64) -> i64 {
    b24(x) + b64(x + 1)
}

