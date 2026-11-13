use crate::base::b14::b14;
use crate::base::b54::b54;

pub fn m90(x: i64) -> i64 {
    b14(x) + b54(x + 1)
}

