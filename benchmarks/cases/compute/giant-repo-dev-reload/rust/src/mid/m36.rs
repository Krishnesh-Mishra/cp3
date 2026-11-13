use crate::base::b108::b108;
use crate::base::b20::b20;

pub fn m36(x: i64) -> i64 {
    b108(x) + b20(x + 1)
}

