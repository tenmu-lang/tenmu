# tmc0 $2014 Tenmu Bootstrap Compiler (Stage 1)

Implementation of **Stage 1 (Frontend)** of the 5-stage implementation plan in `tenmu-spec.md` §15.

C11 only, no external library dependencies.

## Current Status: Stage 1 Completed

- **Lexer** (`src/lexer.c`): All token types, string interpolation `#{}` (nested),

raw strings `r"…"`, byte strings `b"…"`, `\x`/`\u{...}` escapes,

0x/0b/0o numeric literals, block comment nesting,

Implements Go-style automatic semicolon insertion with newlines (ASI).

- **Parser** (`src/parser.c`): Recursive descent parser implementing the entire EBNF from `tenmu-spec.md` §13.

Top-level declarations (fn/struct/enum/error/union/trait/impl/module/import),

Statements, expressions (priority climbing), types, simple pattern matching, closures,

Cover struct literals, if/match/for/while/loop expressions, try/catch, and unsafe blocks.

- **AST** (`include/ast.h`): Type definitions that hold all nodes. Memory is not explicitly freed.

(An intentional simplification for a short-lived compiler that survives until process termination).


## Known Unimplemented Features and Differences from the Specification

During the parser implementation process, several omissions and ambiguities were found in the original EBNF of `tenmu-spec.md`. Therefore, the following have been clarified in this implementation:

- **`for`/`while`/`loop` expressions, named arguments, and tuple decomposition with `let`** were not explicitly stated in the original EBNF, so the syntax has been clarified in this implementation (this has not yet been reflected in `tenmu-spec.md` at the end of the README).

- **`extern module NAME { ... }`** (declarative loading syntax for C extensions, `tenmu-c-extension-spec.md`) is not implemented because it is not reflected in the official EBNF. Attempting to parse it will result in a clear error.
(It will not be misinterpreted as a different syntax).

- **`type X = Y`** (type alias) is also unimplemented.

- Pattern matching supports `_`, literals, tuple variants like `Ok(x)`,

paths like `IoError.NotFound`, and combinations thereof.

Structure decomposition patterns of the form `{ field, .. }` are not supported.

- No semantic analysis is performed, including raw pointer arithmetic, type inference, or borrowing checks (within the scope of Stage 2).

## Build and Test

```sh
make # Generates build/tmc0
make test # Runs all Lexer unit tests, parser integration tests, and the tmc0 core
```

`tmc0 check file.tm` performs syntax checking, `tmc0 items file.tm` displays a list of top-level declarations,

`tmc0 tokens file.tm` dumps the token sequence.

## Test Contents

Actual programs derived from `tenmu-spec.md` and `tenmu-io-spec.md` (examples of combinations of hello world, OS kernel entry point, web server, AI learning steps, and IO operations) are placed in `tests/examples/`, and it has been confirmed that all files pass with zero warnings and zero parsing errors.

In addition, the lexer individually verifies edge cases such as nested string interpolation, non-interpretation of `#{` within raw strings, and virtual semicolon insertion immediately before EOF.

## Next Steps (Stage 2)

Type checker, ownership/borrowing checker, and comptime evaluator. Starting with the symbol table and name resolution is a reasonable initial approach.