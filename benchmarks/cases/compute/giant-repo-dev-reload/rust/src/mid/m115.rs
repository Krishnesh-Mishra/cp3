use crate::base::b89::b89;
use crate::base::b1::b1;

pub fn m115(x: i64) -> i64 {
    b89(x) + b1(x + 1)
}

