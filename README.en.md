# Tenmu Language Specification v0.1 (Draft)

File Extension: `.tm` / CLI Tool Name: `tm`

> A general-purpose programming language aiming to consistently cover everything from bare-metal development of operating systems to web backends and numerical computation for AI/machine learning, all within a single language core. Instead of "forcing unsuitable areas to be covered by extension libraries" like existing languages, it provides a near-native experience in each domain through a design that allows for **explicit switching of execution modes and memory strategies**.

---
## Table of Contents

1. [Overview and Design Philosophy] (#1-Overview and Design Philosophy)
2. [Lexical Structure] (#2-Lexical Structure)
3. [Type System] (#3-Type System)
4. [Memory Model] (#4-Memory Model)
5. [Concurrency] (#5-Concurrency)
6. [Error Handling] (#6-Error Handling)
7. [Module and Package System] (#7-Module and Package System)
8. [Standard Library Structure] (#8-Standard Library Structure)
9. [Compilation and Execution Model] (#9-Compilation and Execution Model)
10. [FFI and Interoperability] (#10-ffi Interoperability)
11. [Toolchain] (#11-Toolchain)
12. [Sample Code] (#12-Sample Code)
13. [Syntax Overview (EBNF Excerpt)] (#13-Syntax Overview EBNF Excerpt)
14. [Unconfirmed Items as of v0.1] (#14-Unconfirmed Items as of v0.1)
15. [Implementation Roadmap] (#15-Implementation Roadmap)

---

## 1. Overview and Design Philosophy

### 1.1 Reasons Why Existing Languages ??Cannot Cover All

| Language | OS/Low Level | Web | AI |
|---|---|---|---|
| C/C++ | ◎ | △ (Low Productivity) | △ (Ecosystem is Primarily via C++) |
| Rust | ◎ | ○ (High Learning Cost of Ownership) | △ (Tensors/Automatic Differentiation are Added Later) |
| Go | △ (GC Required, Kernel Not Possible) | ◎ | △ |
| Python | × (Execution Speed, OS Control Not Possible) | ○ | ◎ |
| Zig | ◎ (Powerful Comptime) | △ (Small Standard Library) | × |

Tenmu aims for the ◎ in this table across all domains by "building every domain on a single language core."

### 1.2 Design Pillars

1. **Single Language, Multiple Targets** $2014 The same source code is compiled for freestanding (kernel/firmware), hosted native (CLI/server), WebAssembly, and accelerator IR (GPU/NPU). Target-specific code is separated by the `#[target(...)]` attribute.

2. **Zero-Cost Abstraction, Cost Visualization** $2014 Generics/traits/iterators are expanded at compile time. Runtime costs such as heap allocation, dynamic dispatch, and GC are always explicitly specified in the source (explicit allocator passing, `dyn`, GC mode specification).

3. **Ownership-Based Safety by Default, Unsafe Explicit** $2014 References are move/borrow checked by default. Raw pointers and inline assembly are only permitted within `unsafe` blocks. This balances kernel-level control with application code safety.

4. **Allocators are Values** (A concept derived from Zig) The $2014 language does not embed a single global allocator. Functions and collections that use the heap explicitly receive an `Allocator` (or are environmentally supplied within the `with allocator:` scope). The same `Vec<T>` implementation code runs directly on the bump allocator in the kernel, the thread cache allocator in the web server, and the GC allocator in scripts.

5. **comptime is the sole metaprogramming mechanism** There are no separate macro or template languages. Generics, reflection, and specialization (e.g., generating fused kernels for specific tensor shapes) are written as ordinary Tenmu code executed at compile time.

6. **Tensors are built-in types** The type system and optimizer understand `Tensor<T, [dims...]>`. The shape is checked at compile time (statically) or runtime (dynamically), and is subject to layout optimization, operation fusion, and automatic differentiation.

### 1.3 High-Level Writing Style

While the system layer adopts a static and explicit design (Rust/Zig-like), when writing `#[managed]` mode (§4.1) or web/script-like code, it employs syntactic sugar prioritizing conciseness, such as string interpolation (`"#{expr}"`), iterator chains that take block arguments (`.map(|x| ...)`), and named arguments. The syntax itself does not branch between the low-level and high-level layers; only the mode switches on the same grammar.

---

## 2. Lexical Structure

- Source file: UTF-8 text, extension `.tm`
- Comments: `// line comments`, `/* block comments (nestable) */`, `/// documentation comments` (attached to the following declaration and extracted by `tm doc`)
- Identifiers: `[A-Za-z_][A-Za-z0-9_]*` (Strings/identifiers in user code can be Unicode. However, reserved words and public symbols in the standard library are ASCII)
- Semicolons are optional (a newline ends a statement. If continuation is syntactically clear, such as with opening parentheses or tail operators, it spans across newlines)
- Attributes: `#[attr]` or `#[attr(args)]`. Attributes written at the beginning of a file apply to the entire module, while attributes written immediately before a declaration apply only to that declaration.
- Numeric literals: `123`, `0x1F`, `0b1010`, `0o17`, `1_000_000` (`_` delimited), `3.14`, `1e10`
- Strings: `"..."` (expressions can be embedded with `#{expr}`), `r"..."` (raw string), `b"..."` (byte sequence), `'a'` (char, Unicode scalar value)
### Reserved words
```
module import pub fn let mut const comptime
struct enum union trait impl
for in while loop if else match
return break continue defer unsafe extern
async await type as where self Self
true false null void error try catch
```
---
## 3. Type System
### 3.1 Primitive Types

- Signed Integers: `i8 i16 i32 i64 i128 isize`
- Unsigned Integers: `u8 u16 u32 u64 u128 usize`
- Floating-Point Numbers: `f16 f32 f64`
- `bool`, `char` (Unicode scalar values, 4 bytes), `void`, `!` (never type. Used for non-returning expressions such as `panic`)

### 3.2 Compound Types

```tenmu
struct Point {
x: f64,
y: f64,
}
enum Shape {
Circle(f64),
Rect(f64, f64),
Triangle { base: f64, height: f64 },
}
#[repr(C)]
union RawValue {
as_i32: i32,
as_f32: f32,
}
// Tuple
let pair: (i32, str) = (1, "one")
```

Object-oriented patterns are expressed using combinations of `struct`/`impl`/`trait`, and there is no separate `class` syntax (the syntax is unified, and it works with the same semantics even in a freestanding environment).

### 3.3 Arrays, Slices, and Collections

- `[N]T` $2014 Fixed-length array (`N` is a compile-time constant)

- `[]T` $2014 Slice (Pointer + length reference view, no ownership)

- `Vec<T>` $2014 Variable-length array (`std.collections`, takes an allocator)

- `str` $2014 Immutable UTF-8 view, `String` $2014 Mutable UTF-8 buffer with ownership

### 3.4 Tensor Type (For AI)

```tenmu
Tensor<f32, [784, 128]> // Static shape (Dimensions checked at compile time)
Tensor<f32, [N, 784]> // N is a const generic (Polymorphic shape)
```

`@` (Matrix multiplication), `+ - * `std.ml.tensor` provides operators/methods such as `/` (element-by-element operation with NumPy-like broadcast rules), `.T` (transposed view), `.reshape()`, and `.sum(axis:)`, and the type checker symbolically checks the shape.

### 3.5 Pointers and References

- `&T` $2014 Shared borrowing (read-only, safe, compile-time check)

- `&mut T` $2014 Exclusive borrowing (safe, one at a time)

- `*T` / `*mut T` $2014 Raw pointer (dereferencing only within `unsafe`)

The borrowing rules are based on "exclusive mutable reference XOR multiple shared references," similar to Rust. Lifetimes are often inferred or omitted, and explicit specification (`'a`) is only required in ambiguous cases, such as when a struct holds a reference.

### 3.6 Optional / Result
```tenmu
let maybe: ?i32 = None // ?T is syntactic sugar for Option<T>
let result: Result<i32, IoError> = Ok(42)
```
Main methods: `.unwrap()` `.unwrap_or(default)` `.is_some()` `.is_none()`. The `?` operator propagates `None`/`Err` with early return.

### 3.7 Functions and Closures
```tenmu
fn add(a: i32, b: i32) -> i32 {
return a + b
}
// Default Arguments and Named Arguments
fn connect(host: str, port: i32 = 8080) -> Connection { ... }
connect("localhost", port: 9000)
// Closures (Sharing Rust/Ruby's | | notation)
let square = |x: i32| -> i32 { x * x }
values.map(|x| x * 2).filter(|x| x > 0)
```
Functions are first-class values ??and can be assigned to variables or passed as arguments.

### 3.8 Trait Generics
```tenmu
trait Animal {
fn name(&self) -> str
fn speak(&self) -> str { return "..." } // Default implementation
}
impl Animal for Dog {
fn name(&self) -> str { return "Dog" }
fn speak(&self) -> str { return "Woof" }
}
fn max<T: Ord>(a: T, b: T) -> T {
if a > b { return a } else { return b }
}
```
Generics are monomorphized at compile time by default (zero cost). Explicitly specify `dyn Trait` only when dynamic dispatch is required (disabled by default in `#[target(*-freestanding)]` builds using vtables; explicitly enable if necessary).

### 3.9 comptime
```tenmu
comptime fn square(x: i32) -> i32 {
return x * x
}
const BUF_SIZE: usize = square(8) // Evaluates to 64 at compile time

// Example of shape-specific kernel generation
comptime fn make_matmul_kernel<const M: usize, const N: usize, const K: usize>() -> Kernel {
// Generated by selecting loop unrolling and SIMD width specialized for M, N, K
}
```

comptime code is executed on a sandboxed value model that does not allow raw pointer access to host I/O or physical memory (with the exception of explicit built-in functions such as `embed_file()`).

---
## 4. Memory Model

### 4.1 Execution Modes

Three modes are switched depending on the compilation target and attributes, even with the same syntax.

| Mode | Determination Method | Features |
|---|---|---|
| Freestanding | `--target=*-freestanding` | No OS, no default allocator, only `std.core` is usable. The allocator must be provided by the user.
| Hosted (Default) | `--target=*-linux/windows/macos/wasm32-*` | `std.os`/`std.net`/`std.io` are usable. A default general-purpose allocator is available (replaceable).
| Managed | `#[managed]` attribute (Host target only) | Ambient tracing GC (`std.gc`) becomes the default allocator. Reducing the ritual of ownership checks and simplifying the description of web handlers, etc. |

### 4.2 Ownership and Borrowing

Assignments of non-`Copy` types are treated as moves, while `&`/`&mut` are treated as borrows (similar to Rust). Small POD structs and primitive types are implicitly copied as `Copy`.

### 4.3 Allocators

`std.mem.Allocator` is a trait with implementations such as `PageAllocator` (OS-backed), `ArenaAllocator`, `BumpAllocator` (suitable for freestanding), `GcAllocator` (managed mode), and `PoolAllocator`. Functions that use the heap either explicitly accept it or are environmentally supplied with `with allocator: a { ... }` scope (this syntactic sugar expands to explicit argument passing at compile time, resulting in zero runtime cost).

### 4.4 defer / RAII
```tenmu
fn read_config(a: &Allocator, path: str) -> Result<Config, IoError> {
let f = fs.open(path)?
defer f.close() // Execute LIFO when leaving scope

...
}
```
Types that implement the `Drop` trait are automatically cleaned up when leaving scope and can be used with `defer`.

---
## 5. Concurrency
```tenmu
import std.thread
import std.async
// OS threads (low-level, can be used on a custom scheduler even if freestanding)
fn worker_example() {
let t = thread.spawn(|| {
io.println("worker thread")
})
t.join()
}
// async/await with structured concurrency (for Web)
async fn fetch_all(urls: []str) -> []Result<str, HttpError> {
let tasks = urls.map(|u| async.spawn(|| fetch(u)))
return await async.join_all(tasks)
}
```

- Low-level: `std.thread` (OS threads), `std.atomic` (atomic operations, memory ordering)
- Hosted async: A lightweight M:N cooperative scheduler based on `std.async`. Tasks created with `async.spawn` cannot survive beyond the scope of their origin (structured concurrency, leak prevention).
- Message passing: `Channel<T>` (MPSC/MPMC) and `select` expressions that listen on multiple channels.
- `await` is only needed at true breakpoints; at the "edges" of synchronous code such as `main`, use `async.block_on` to bridge the gap.
---
## 6. Error Handling

```tenmu
error MathError {
DivByZero,
Overflow,
}
fn divide(a: i32, b: i32) -> Result<i32, MathError> {
if b == 0 {
return Err(MathError.DivByZero)
}
return Ok(a / b)
}
fn compute() -> Result<i32, MathError> {
try {
let x = divide(10, 2)?
let y = divide(x, 0)?
return Ok(y)
} catch (e) {
io.println("Error: #{e}")
return Err(e)
}
}
```
The `error` declaration is syntactic sugar for an `enum` that automatically implements the `Error` trait. It does not have an exception mechanism, and `panic()` is for irrecoverable errors (in freestanding, the user provides a panic handler; in hosted types, it displays a message + backtrace and terminates by default; in `#[managed]`, there is also the option to unwind up to the `catch_unwind` boundary).

---

## 7. Module Package System

```tenmu
module myapp.handlers // Inferred from directory structure if omitted

import std.net.http
import "./util.tm" as util
import "github.com/alice/mathutils"
```

- Explicitly specify public symbols with `pub` (default is private module)
- **Package management uses the Go method (distributed, VCS path-based). There is no central registry.** The import path for third-party packages directly represents the location on the VCS (e.g., `github.com/alice/mathutils`). "Publishing" is completed simply by setting a version tag (`v1.2.0`, etc.) on the VCS side, and `tm get` directly retrieves the repository at the specified path. - Does not require separate name registration and upload procedures like npm/crates.io/PyPI
- Uses **Minimal Version Selection (MVS)** for dependency resolution. When multiple dependencies require different versions of the same package, it selects the **minimum** version that satisfies the requirements (the opposite of the npm/cargo method of selecting the "latest version that satisfies the requirements"). This prevents builds from becoming deterministic without a SAT solver for lock files, and prevents transitive dependencies from being silently upgraded to the latest version.
- Major versions with breaking changes are embedded in the import path itself (`github.com/alice/mathutils/v2`). This allows v1 and v2 to coexist within the same program (Semantic Import Versioning).
- Package manifest `tenmu.toml` (TOML syntax remains the same, but the content is adapted to Go-style semantics):

```toml
[module]
path = "github.com/myorg/myapp" # The official path when publishing itself. This can be omitted for standalone applications.
tenmu = "0.1" # Required Tenmu language version
[require]
"github.com/alice/mathutils" = "v1.2.0"
"github.com/bob/httprouter" = "v0.9.1"
```

- The retrieved dependencies are stored in the local cache (e.g., `~/.tenmu/pkg/mod/`), and the checksum of each dependency is recorded in `tenmu.sum`. During the build process, `tenmu.sum` is compared to detect tampering or corruption (equivalent to Go's `go.sum`). While there is room for providing optional proxy services to assist with caching/tamper detection in the future, the basic distributed configuration is always self-contained even with direct VCS acquisition.
- Standard libraries (`std.*`) are bundled with the compiler and are not subject to this mechanism.
---
## 8. Standard Library Structure

| Module | Supported Modes | Content |
|---|---|---|
| `std.core` | All Modes | Primitives, memory operations such as memcpy, mathematical functions (zero external dependencies) |
| `std.mem` | All Modes | `Allocator` trait and implementations |
| `std.collections` | All Modes | `Vec` `HashMap` `HashSet` `Deque` `BTreeMap` |
| `std.random` | All Modes | Seedable pseudo-random number generator (`Rng`), uniform/normal distribution, shuffle. Freestanding requires explicit seeding; hosted uses OS entropy (`getrandom`, etc.) as the default seed source |
| `std.io` | Hosted/managed | `Read`/`Write`/`Seek` traits, buffering, stdin/stdout/stderr (details: `tenmu-io-spec.md`) |
| `std.fs` | Hosted | File, directory, and path operations (details: `tenmu-io-spec.md`) |
| `std.os` | Hosted | Process, environment variables, and signals |
| `std.os.mem` / `std.os.syscall` | Freestanding possible / Hosted | Paging, MMIO, port I/O, raw syscall (`unsafe` required. Details: `tenmu-io-spec.md`) |
| `std.thread` / `std.atomic` / `std.async` | All modes / All modes`std.net` / `std.net.http` | Hosted | TCP/UDP/TLS sockets, HTTP/1.1 and HTTP/2 client-server |
| `std.encoding` | Hosted/managed | JSON, base64, UTF-8/UTF-16 codecs |
| `std.wasm` | wasm32-* | DOM/JS interoperability bindings |
| `std.ml.tensor` | Hosted | Tensor operations, broadcasting, linear algebra |
| `std.ml.autodiff` | Hosted | Forward and inverse mode automatic differentiation |
| `std.ml.nn` / `std.ml.optim` | Hosted | Standard layers (Linear, Conv2d, Attention, LayerNorm, etc.), optimizers (SGD, Adam, etc.) |
| `std.ml.accel` | Host type | Compiles and dispatches `#[kernel]` functions to accelerator IR (PTX/SPIR-V/Metal) |
| `std.gc` | `#[managed]` | Opt-in tracing GC |

---

## 9. Compilation and Execution Model

- Default is AOT compilation to native machine code. `tm run file.tm` compiles and immediately executes using an unoptimized, high-speed code generation path, providing a script-like development experience.
- **Backend Configuration** (Pluggable):
- Native: Tenmu IR (TIR) ??→ Custom backend (for Debug/`tm run`, high-speed compilation) or via LLVM IR (optimized for Release, expanded architecture support)
- WebAssembly: TIR → Direct conversion to WASM bytecode (no LLVM required, keeps the web target toolchain lightweight)
- Accelerator: Only functions with the `#[kernel]` attribute are converted from TIR to PTX / SPIR-V / Metal Shading Language with limitations. Selected independently of the host program's `--target` using `--accel=`, and dispatched from the host code at runtime.
- Comptime evaluation uses a sandboxed compile-time interpreter (in the future, Tenmu's own native backend will be reused for compile-time execution for faster performance).
- Build Profiles: `Debug` (fast compilation, all checks enabled), `ReleaseSafe` (optimization + boundary/overflow checks maintained), `ReleaseFast` (optimization, checks removed), `ReleaseSmall` (prioritize binary size. For kernel/firmware/WASM payloads).
- Target Triples: `x86_64-linux` `aarch64-linux` `x86_64-windows` `aarch64-macos` `x86_64-freestanding` `aarch64-freestanding` `wasm32-web` `wasm32-wasi`

---

## 10. FFI Interoperability
```tenmu
extern "C" fn tenmu_add(a: i32, b: i32) -> i32 {
return a + b
}
#[repr(C)]
struct CPoint { x: f64, y: f64 }
```

- `extern "C"` allows exposure/inclusion of C ABI functions (no overhead, static linking at compile time). Used when integrating with existing C kernels/drivers in OS development, or when reusing existing C/C++ assets.
- `#[repr(C)]` specifies a C-compatible memory layout.
- **C Extension (Dynamic Loading)**: While `extern "C"` uses static linking, `#[c_extension("libfoo.so")] extern module` declaration or `std.dl` allows loading `.so`/`.dll` at runtime, extending Tenmu programs in a manner equivalent to native CPython/Ruby extensions. For details on the stable C API (`tenmu_ext.h`), type marshalling, GC boundary handling, etc., please refer to `tenmu-c-extension-spec.md`.
- `std.py` (optional, host type only): Generates CPython ABI-compatible extension modules from the `#[py_export]` function, or embeds a Python interpreter, bridging the gap between the existing PyTorch/NumPy/HuggingFace ecosystem and `std.ml` until it matures.
- `std.wasm.js`: Typed bindings supporting JS function calls/calls from JS.

---

## 11. Toolchain

Complete with a single `tm` command (compiler diagnostic messages, CLI output, and documentation generated by `tm doc` are in English by default. User-defined identifiers in the source code themselves are allowed in Unicode).

- `tm ??build` $2014 Compile
- `tm ??run` $2014 Compile + Execute immediately
- `tm ??test` $2014 Execute the `#[test]` function
- `tm ??fmt` $2014 Standard formatter (similar to `gofmt`, no style selection)
- `tm ??doc` $2014 Generate documentation from `///` comments
- `tm ??get <path>@<version>` $2014 Add/update dependencies (Go style, retrieve directly from VCS)
- `tm ??mod tidy` $2014 Organize `[require]` in `tenmu.toml` to match actual usage
- `tm ??mod verify` $2014 Detect dependency tampering/corruption by comparing with `tenmu.sum`

---

## 12. Sample Code

### Hello World
```tenmu
import std.io
fn main() -> i32 {
io.println("Hello, Tenmu!")
return 0
}
```

### OS Kernel Entry Point
```tenmu
#[target(x86_64-freestanding)]
module kernel
import std.os.mem
extern "C" fn _start() -> ! {
let vga: *mut u16 = mem.mmio(0xB8000)
unsafe {
*vga = 0x0F41 // Display a single 'A' on a white background
}
loop {}
}
```

### Web Server
```tenmu
import std.io
import std.net.http
import std.encoding.json
struct Greeting {
message: str,
}

async fn handle(req: http.Request) -> http.Response { 
let name = req.query("name").unwrap_or("world") 
let body = Greeting { message: "Hello, #{name}!" } 
return http.Response.json(200, json.encode(body))
}

fn main() { 
http.serve(":8080", handle)
}
````

### AI: Learning Steps

```tenmu
import std.io
import std.ml.tensor
import std.ml.nn
import std.ml.autodiff as ad
import std.ml.optim

struct Params { 
w1: Tensor<f32, [784, 128]>, 
w2: Tensor<f32, [128, 10]>,
}

fn model<const N: usize>(x: Tensor<f32, [N, 784]>, p: &Params) -> Tensor<f32, [N, 10]> { 
let h = nn.relu(x @ p.w1) 
return nn.softmax(h @ p.w2)
}

fn train_step<const N: usize>( 
x: Tensor<f32, [N, 784]>, 
y: Tensor<f32, [N, 10]>, 
params: &mut Params, 
opt: &mut optim.Sgd,
) { 
let (loss, grads) = ad.value_and_grad(params, |p| { 
return nn.cross_entropy(model(x, p), y) 
}) 
opt.step(params, grads) 
io.println("loss = #{loss}")
}
````

---

## 13. Grammar overview (EBNF excerpt)

```ebnf
Program ::= Item*
Item ::= ModuleDecl | ImportDecl | FnDecl | StructDecl | EnumDecl 
| UnionDecl | TraitDecl | ImplDecl | ErrorDecl | Attribute Item

ModuleDecl ::= "module" Path
ImportDecl ::= "import" Path ("as" Ident)?
Attribute ::= "#[" Ident ("(" AttrArgs ")")? "]"

FnDecl ::= "pub"? "comptime"? "async"? ("extern" StringLit)? 
"fn" Ident GenericParams? "(" ParamList? ")" ("->" Type)? (Block | ";")
GenericParams ::= "<" GenericParam ("," GenericParam)* ">"
GenericParam ::= Ident (":" Bound ("+" Bound)*)? | "const" Ident ":" Type
ParamList ::= Param ("," Param)*
Param ::= Ident ":" Type ("=" Expr)?

StructDecl ::= "pub"? "struct" Ident GenericParams? "{" FieldList? "}"
Field ::= "pub"? Ident ":" Type
EnumDecl ::= "pub"? "enum" Ident GenericParams? "{" VariantList? "}"
Variant ::= Ident ("(" TypeList ")" | "{" FieldList "}")?
ErrorDecl ::= "pub"? "error" Ident "{" VariantList? "}"

TraitDecl ::= "pub"? "trait" Ident GenericParams? "{" FnDecl* "}"
ImplDecl ::= "impl" GenericParams? Type ("for" Type)? "{" FnDecl* "}"

Type ::= PathType 
| "*" "mut"? Type | "&" "mut"? Type 
| "[" Expr? "]" Type 
| "(" TypeList? ")" 
| "Tensor" "<" Type "," "[" ExprList "]" ">" 
| "?" Type | "!" 
| "fn" "(" TypeList? ")" ("->" Type)?
PathType ::= Ident ("<" TypeList ">")? ("." Ident)*

Stmt ::= LetStmt | ExprStmt | "return" Expr? | "break" | "continue" 
| "defer" Expr | Item
LetStmt ::= "let" "mut"? Ident (":" Type)? "=" Expr

Expr ::= AssignExpr
AssignExpr ::= OrExpr (("=" | "+=" | "-=" | "*=" | "/=") AssignExpr)?
OrExpr ::= AndExpr ("||" AndExpr)*
AndExpr ::= CmpExpr ("&&" CmpExpr)*
CmpExpr ::= BitOrExpr (("==" | "!=" | "<" | ">" | "<=" | ">=") BitOrExpr)?
BitOrExpr ::= BitXorExpr ("|" BitXorExpr)*
BitXorExpr ::= BitAndExpr ("^" BitAndExpr)*
BitAndExpr ::= ShiftExpr ("&" ShiftExpr)*
ShiftExpr ::= AddExpr (("<<" | ">>") AddExpr)*
AddExpr ::= MulExpr (("+" | "-") MulExpr)*
MulExpr ::= CastExpr (("*" | "/" | "%" | "@") CastExpr)*
CastExpr ::= UnaryExpr ("as" Type)*
UnaryExpr ::= ("-" | "!" | "*" | "&" "mut"?) UnaryExpr | PostfixExpr
PostfixExpr ::= PrimaryExpr ("." Ident | "(" ArgList? ")" | "[" Expr "]" | "?")*
PrimaryExpr ::= Literal | Ident | "(" Expr ")" | Block
| IfExpr | MatchExpr | "try" Block "catch" "(" Ident ")" Block
| ClosureExpr | "unsafe" Block
ClosureExpr ::= "|" ParamList? "|" (Expr | Block)
IfExpr ::= "if" Expr Block ("else" (IfExpr | Block))?
MatchExpr ::= "match" Expr "{" (Pattern "=>" Expr ",")* "}"
```
---
## 14. Unconfirmed items as of v0.1

- Map/Dictionary literal syntax (Currently via the `HashMap` constructor. Dedicated literals will be considered in the future)
- - Formal rules for the borrowing checker (whether to go as far as non-lexical lifetimes similar to Rust, or to stick to a simplified version, will be decided during implementation)
- Scope of accelerators officially supported by `std.ml.accel` (how far will CUDA/ROCm/Metal/Vulkan be supported initially)
- Whether to provide an optional cache/tamper detection proxy (equivalent to Go's `sum.golang.org`) in the future, and if so, who will host it (as resolved in §7, it can be completed by directly obtaining the VCS without providing it)
- Scope of support for OR/AND expressions in conditional compilation attributes (`#[target(...)]`)
- Whether to maintain the minimal C runtime `libtmrt` after self-hosting, or to replace it with freestanding Tenmu code in the future
- How to handle `tmc0` (C implementation) after achieving self-hosting (whether to completely freeze it, or continue maintaining it as a bootstrap seed for porting to new platforms)

---
## 15. Implementation Roadmap (5 Stages)

**Implementation Language**: Stage 1: $301C4 will be implemented in C as the bootstrap compiler `tmc0`. The standard library itself will be written in Tenmu from the beginning (only the compiler body will be in C; `std.io` etc. will be written as Tenmu code from the beginning and compiled with `tmc0`). In Stage 5, the compiler body will be rewritten in Tenmu to achieve self-hosting, and all subsequent development (WASM/`std.net`/`std.ml` etc.) will be done on the self-hosted Tenmu implementation side. Lexar and comptime evaluators, which are prone to breaking boundary conditions, will be verified with fuzz tests.

### Stage 1 - Frontend
Lexar (tokenizer), parser, and AST construction. Reads `.tm` source and generates an AST before type checking. Includes tokenization of string interpolation `#{}` and automatic semicolon insertion due to newlines.

### Stage 2 - Semantic Analysis
Type checker, ownership/borrowing checker, comptime evaluator (sandboxed compile-time interpreter).

### Stage 3 - Code Generation & Minimal Runtime
Custom native backend (first x86-64 Linux/Windows). Implement the minimal C runtime `libtmrt` (allocator primitives, panic handler, C extension `dlopen` loader, API implementation for `tenmu_ext.h`). At this point, `tmc0` is ready to actually compile `.tm` files into executables.

### Stage 4 - Core Standard Library + C Extension API Establishment
Tenmu implements `std.core` / `std.mem` / `std.collections` / `std.io` / `std.fs` and compiles them with `tmc0` (details: `tenmu-io-spec.md`). `tenmu_ext.h` Finalize the C extension API and verify its operation with the reference extension (details: `tenmu-c-extension-spec.md`). Prioritizing `std.io`/`std.fs` is essential because it's a necessary dependency for the compiler written by Tenmu itself in Stage 5 to read and write source files.

### Stage 5 - Self-Hosting
Rewrite the compiler body `tmc` in Tenmu. Compile the `tmc` source with `tmc0` to obtain `tmc1`. Recompile the same `tmc` source with `tmc1` to obtain `tmc2`. Self-hosting is achieved when the behavior of `tmc1` and `tmc2` matches (pass all test suites, and if possible, compare with a third build in a "triple build" to confirm stability). After achievement, `tmc0` is frozen (retained only as a bootstrap seed for porting to new platforms), and all subsequent development continues on the Tenmu side (`tmc`).