use crate::base::b37::b37;
use crate::base::b77::b77;

pub fn m55(x: i64) -> i64 {
    b37(x) + b77(x + 1)
}

