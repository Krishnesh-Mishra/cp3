use crate::base::b42::b42;
use crate::base::b82::b82;

pub fn m14(x: i64) -> i64 {
    b42(x) + b82(x + 1)
}

