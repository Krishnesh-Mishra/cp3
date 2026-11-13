use crate::base::b112::b112;
use crate::base::b24::b24;

pub fn m80(x: i64) -> i64 {
    b112(x) + b24(x + 1)
}

