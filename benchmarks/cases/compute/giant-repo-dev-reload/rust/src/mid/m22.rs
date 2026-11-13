use crate::base::b66::b66;
use crate::base::b106::b106;

pub fn m22(x: i64) -> i64 {
    b66(x) + b106(x + 1)
}

