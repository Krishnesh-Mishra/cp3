use crate::base::b3::b3;
use crate::base::b43::b43;

pub fn m1(x: i64) -> i64 {
    b3(x) + b43(x + 1)
}

