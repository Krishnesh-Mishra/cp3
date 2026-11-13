use crate::base::b88::b88;
use crate::base::b0::b0;

pub fn m72(x: i64) -> i64 {
    b88(x) + b0(x + 1)
}

