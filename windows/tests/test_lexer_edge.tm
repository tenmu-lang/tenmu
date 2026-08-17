// line comment
/* block /* nested */ comment */
#[target(x86_64-freestanding)]
#[c_extension("libfastmath.so")]
fn f() {
    let a = "a #{ if x { "b" } else { "c" } } d"
    let b = "multi #{x} mid #{y} end"
    let r = r"raw \n not-an-escape #{not interp}"
    let bs = b"bytes\n"
    let esc = "\n\t\\\"\u{1F600}"
    let ops = 1 <= 2 >= 3 == 4 != 5 << 6 >> 7 && 8 || 9
    a += 1
    a -= 1
    a *= 2
    a /= 2
    let range = 1..10
    let hex = 0xFF
    let bin = 0b1010
    let oct = 0o17
}
let no_trailing_newline_ok = 1
