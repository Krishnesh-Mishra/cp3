use crate::base::b41::b41;
use crate::base::b81::b81;

pub fn m99(x: i64) -> i64 {
    b41(x) + b81(x + 1)
}

