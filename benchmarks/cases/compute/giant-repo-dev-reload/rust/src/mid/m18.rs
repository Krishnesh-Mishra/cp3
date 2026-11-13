use crate::base::b54::b54;
use crate::base::b94::b94;

pub fn m18(x: i64) -> i64 {
    b54(x) + b94(x + 1)
}

