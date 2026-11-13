use crate::base::b61::b61;
use crate::base::b101::b101;

pub fn m63(x: i64) -> i64 {
    b61(x) + b101(x + 1)
}

