use crate::base::b62::b62;
use crate::base::b102::b102;

pub fn m106(x: i64) -> i64 {
    b62(x) + b102(x + 1)
}

