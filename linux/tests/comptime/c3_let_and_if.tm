comptime fn classify(x: i32) -> bool {
    let doubled = x * 2
    if doubled > 100 {
        return true
    } else {
        return false
    }
}

const IS_BIG: bool = classify(60)
const IS_SMALL: bool = classify(10)

fn main() -> i32 {
    return 0
}
