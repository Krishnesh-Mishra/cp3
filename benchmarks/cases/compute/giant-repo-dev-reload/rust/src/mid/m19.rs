use crate::base::b57::b57;
use crate::base::b97::b97;

pub fn m19(x: i64) -> i64 {
    b57(x) + b97(x + 1)
}

