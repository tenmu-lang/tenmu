struct Data { x: i32 }
fn read(d: &Data) -> i32 { return d.x }
fn main() -> i32 {
    let a = Data { x: 1 }
    let r1 = &a
    let r2 = &a
    return read(r1) + read(r2)
}
