use crate::base::b43::b43;
use crate::base::b83::b83;

pub fn m57(x: i64) -> i64 {
    b43(x) + b83(x + 1)
}

