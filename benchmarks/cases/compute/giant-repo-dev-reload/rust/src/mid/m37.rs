use crate::base::b111::b111;
use crate::base::b23::b23;

pub fn m37(x: i64) -> i64 {
    b111(x) + b23(x + 1)
}

