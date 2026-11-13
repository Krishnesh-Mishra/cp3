use crate::base::b13::b13;
use crate::base::b53::b53;

pub fn m47(x: i64) -> i64 {
    b13(x) + b53(x + 1)
}

