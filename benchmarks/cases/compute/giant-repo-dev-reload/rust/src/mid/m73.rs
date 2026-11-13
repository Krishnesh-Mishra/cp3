use crate::base::b91::b91;
use crate::base::b3::b3;

pub fn m73(x: i64) -> i64 {
    b91(x) + b3(x + 1)
}

