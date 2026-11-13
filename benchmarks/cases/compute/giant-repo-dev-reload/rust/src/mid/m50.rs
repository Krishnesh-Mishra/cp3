use crate::base::b22::b22;
use crate::base::b62::b62;

pub fn m50(x: i64) -> i64 {
    b22(x) + b62(x + 1)
}

