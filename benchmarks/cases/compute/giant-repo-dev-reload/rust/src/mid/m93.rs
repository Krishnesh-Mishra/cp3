use crate::base::b23::b23;
use crate::base::b63::b63;

pub fn m93(x: i64) -> i64 {
    b23(x) + b63(x + 1)
}

