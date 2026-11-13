use crate::base::b65::b65;
use crate::base::b105::b105;

pub fn m107(x: i64) -> i64 {
    b65(x) + b105(x + 1)
}

