use crate::base::b51::b51;
use crate::base::b91::b91;

pub fn m17(x: i64) -> i64 {
    b51(x) + b91(x + 1)
}

