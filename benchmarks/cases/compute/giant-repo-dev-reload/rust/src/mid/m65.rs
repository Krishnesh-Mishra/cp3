use crate::base::b67::b67;
use crate::base::b107::b107;

pub fn m65(x: i64) -> i64 {
    b67(x) + b107(x + 1)
}

