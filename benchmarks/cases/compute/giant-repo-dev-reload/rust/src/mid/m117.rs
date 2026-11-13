use crate::base::b95::b95;
use crate::base::b7::b7;

pub fn m117(x: i64) -> i64 {
    b95(x) + b7(x + 1)
}

