use crate::base::b109::b109;
use crate::base::b21::b21;

pub fn m79(x: i64) -> i64 {
    b109(x) + b21(x + 1)
}

