use crate::base::b36::b36;
use crate::base::b76::b76;

pub fn m12(x: i64) -> i64 {
    b36(x) + b76(x + 1)
}

