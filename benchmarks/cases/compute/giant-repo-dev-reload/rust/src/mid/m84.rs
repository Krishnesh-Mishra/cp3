use crate::base::b124::b124;
use crate::base::b36::b36;

pub fn m84(x: i64) -> i64 {
    b124(x) + b36(x + 1)
}

