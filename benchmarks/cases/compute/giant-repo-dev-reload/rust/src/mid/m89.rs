use crate::base::b11::b11;
use crate::base::b51::b51;

pub fn m89(x: i64) -> i64 {
    b11(x) + b51(x + 1)
}

