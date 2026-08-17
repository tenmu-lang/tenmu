fn main() -> i32 {
    let x: i32 = 5
    let p: *i32 = &x as *i32
    let v = *p
    return 0
}
