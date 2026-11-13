use crate::base::b47::b47;
use crate::base::b87::b87;

pub fn m101(x: i64) -> i64 {
    b47(x) + b87(x + 1)
}

