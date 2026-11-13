use crate::base::b28::b28;
use crate::base::b68::b68;

pub fn m52(x: i64) -> i64 {
    b28(x) + b68(x + 1)
}

