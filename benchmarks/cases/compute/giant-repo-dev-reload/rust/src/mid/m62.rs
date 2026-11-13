use crate::base::b58::b58;
use crate::base::b98::b98;

pub fn m62(x: i64) -> i64 {
    b58(x) + b98(x + 1)
}

