struct Data { x: i32 }
fn take(d: Data) -> i32 { return d.x }
fn main() -> i32 {
    let a = Data { x: 1 }
    let r = &a
    return take(a)
}
