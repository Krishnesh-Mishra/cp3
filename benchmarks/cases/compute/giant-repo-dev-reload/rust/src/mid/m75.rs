use crate::base::b97::b97;
use crate::base::b9::b9;

pub fn m75(x: i64) -> i64 {
    b97(x) + b9(x + 1)
}

