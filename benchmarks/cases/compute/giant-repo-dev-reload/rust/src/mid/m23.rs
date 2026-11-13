use crate::base::b69::b69;
use crate::base::b109::b109;

pub fn m23(x: i64) -> i64 {
    b69(x) + b109(x + 1)
}

