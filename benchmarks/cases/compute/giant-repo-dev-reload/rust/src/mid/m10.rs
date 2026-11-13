use crate::base::b30::b30;
use crate::base::b70::b70;

pub fn m10(x: i64) -> i64 {
    b30(x) + b70(x + 1)
}

