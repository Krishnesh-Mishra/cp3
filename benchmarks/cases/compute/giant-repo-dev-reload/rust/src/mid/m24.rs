use crate::base::b72::b72;
use crate::base::b112::b112;

pub fn m24(x: i64) -> i64 {
    b72(x) + b112(x + 1)
}

