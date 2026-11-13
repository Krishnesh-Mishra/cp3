use crate::base::b74::b74;
use crate::base::b114::b114;

pub fn m110(x: i64) -> i64 {
    b74(x) + b114(x + 1)
}

