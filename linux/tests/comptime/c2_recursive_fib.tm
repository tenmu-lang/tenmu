comptime fn fib(n: i32) -> i32 {
    if n <= 1 {
        return n
    }
    return fib(n - 1) + fib(n - 2)
}

const FIB10: i32 = fib(10)

fn main() -> i32 {
    return 0
}
