use crate::base::b83::b83;
use crate::base::b123::b123;

pub fn m113(x: i64) -> i64 {
    b83(x) + b123(x + 1)
}

