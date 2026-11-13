use crate::base::b106::b106;
use crate::base::b18::b18;

pub fn m78(x: i64) -> i64 {
    b106(x) + b18(x + 1)
}

