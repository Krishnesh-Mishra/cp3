use crate::base::b121::b121;
use crate::base::b33::b33;

pub fn m83(x: i64) -> i64 {
    b121(x) + b33(x + 1)
}

