use crate::base::b1::b1;
use crate::base::b41::b41;

pub fn m43(x: i64) -> i64 {
    b1(x) + b41(x + 1)
}

