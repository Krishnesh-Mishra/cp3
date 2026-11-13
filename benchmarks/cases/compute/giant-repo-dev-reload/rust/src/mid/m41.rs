use crate::base::b123::b123;
use crate::base::b35::b35;

pub fn m41(x: i64) -> i64 {
    b123(x) + b35(x + 1)
}

