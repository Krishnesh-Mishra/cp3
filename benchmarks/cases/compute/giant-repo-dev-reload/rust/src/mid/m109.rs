use crate::base::b71::b71;
use crate::base::b111::b111;

pub fn m109(x: i64) -> i64 {
    b71(x) + b111(x + 1)
}

