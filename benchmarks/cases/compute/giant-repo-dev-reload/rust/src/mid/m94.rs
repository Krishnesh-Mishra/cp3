use crate::base::b26::b26;
use crate::base::b66::b66;

pub fn m94(x: i64) -> i64 {
    b26(x) + b66(x + 1)
}

