use crate::base::b49::b49;
use crate::base::b89::b89;

pub fn m59(x: i64) -> i64 {
    b49(x) + b89(x + 1)
}

