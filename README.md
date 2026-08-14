# ZCompiler

A compiler for **Z**, a custom multi-paradigm language, written in C++17 against the LLVM C++ API. This is a learning project — the goal is to understand how compilers work by building one end to end, from tokenizer to linked executable.

The full design, milestone breakdown, and architectural decisions live in [docs/Plan.md](docs/Plan.md).

## Status

Milestones **M0–M2** are complete; **M3** is in progress.

| Milestone | Scope | State |
|---|---|---|
| M0 | `print` + `return` compiling to a working `.exe` | done |
| M1 | `let`, integer arithmetic, assignment | done |
| M2 | Functions, `if`/`else`, `while`, `for`, `do…along`, `switch`, `break`, `continue` | done |
| M3 | Full type system — `string`, `dynamic`, `null`, ternary, `static_cast` | in progress |

M3 currently has the scalar primitives (`int`, `int32`, `int64`, `int128`, `float16`, `float`, `double`, `bool`, `char`, `string` literals). Still to land: `string` as a real runtime type with `+` and comparisons, `dynamic`, `null`, the ternary operator, and `static_cast` parsing. See the M3 section of [docs/Plan.md](docs/Plan.md) for the ordered work list.

## Building

Requires **LLVM 22** development libraries, CMake 3.20+, and a C++17 compiler. The reference setup is MSYS2 UCRT64:

```bash
pacman -Syu                                          # do this first — see below
pacman -S mingw-w64-ucrt-x86_64-llvm mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja
```

```bash
cmake -S . -B build -G Ninja
cmake --build build
```

This produces `build/zc`. If CMake complains that the cache was created in a different directory, delete `build/` and re-run the configure step.

**Two environment traps worth knowing**, both of which produce failures that look like compiler bugs:

- **Do a full `pacman -Syu`, not a targeted install.** MSYS2 does not support partial upgrades. Installing LLVM without upgrading the rest pulls in a build made against a newer GCC runtime, and `zc.exe` then dies at startup with `STATUS_ENTRYPOINT_NOT_FOUND` (exit `127` from bash, `-1073741511` from PowerShell) — no diagnostic, nothing on stderr.
- **`zc` links the shared `libLLVM`, so LLVM's `bin` directory must be on `PATH`** to run it. `Test/run_tests.sh` handles this automatically by reading the path CMake records in `build/llvm_bin_dir.txt`, but invoking `build/zc.exe` by hand from a shell without `C:\msys64\ucrt64\bin` on `PATH` fails the same silent way.

CMake links the single shared `libLLVM` when the distribution provides one (MSYS2 sets `LLVM_LINK_LLVM_DYLIB=ON`) and falls back to static component libraries otherwise. The static path is a genuine fallback rather than a preference: linking ~70 static LLVM archives exhausts the BFD linker, which fails with a bare `ld returned 5 exit status` and no further explanation.

> From M17 onward the tensor library adds an **MLIR** dependency, version-locked to LLVM — `MLIRConfig.cmake` does `find_package(LLVM ... EXACT)`, so the two must be the same version down to the patch. It is gated behind `-DZ_ENABLE_MLIR=ON` so everything below M17 builds without it. See the prerequisite section of [docs/Plan.md](docs/Plan.md).

## Usage

```bash
zc program.z -o program.exe    # compile and link (-O2 by default)
zc -O0 program.z -o program.exe
zc --dump-tokens program.z     # token stream
zc --dump-ast program.z        # AST (partial — see Known gaps)
zc --emit-llvm program.z       # LLVM IR, after the optimisation pipeline
zc -O0 --emit-llvm program.z   # raw CodeGen output, no passes
```

`-O0` through `-O3` select the LLVM pass pipeline; `-O2` is the default. `--emit-llvm` prints the module *after* the pipeline has run, so pairing it with `-O0` and `-O2` shows exactly what the passes did.

A program:

```z
fn factorial(n: int) -> int {
    if (n <= 1) { return 1 }
    return n * factorial(n - 1)
}

fn main() -> int {
    for (let i: int = 1; i <= 5; i = i + 1) {
        if (i % 2 == 0) { continue }
        print(factorial(i))
    }
    return 0
}
```

Statements are newline-terminated (no semicolons), declarations use `let name: type = value`, and conditions must be parenthesised.

## Testing

```bash
./Test/run_tests.sh              # everything
./Test/run_tests.sh switch       # only tests whose name contains "switch"
cmake --build build --target check
```

The runner exits non-zero if anything fails, so it works as a CI gate.

Two suites, discovered by file layout — dropping in a `.z` file and its expected-output companion is all it takes to add a case:

| Suite | Files | Check |
|---|---|---|
| `Test/codegen/` | `*.z` + `*.expected` | Compile, run, compare stdout — **once per optimisation level** |
| `Test/sema/` | `*.z` + `*.expected-error` | Must **fail** to compile; diagnostic must contain the expected text |

`Test/codegen/` holds both the per-milestone tour programs (`m0_hello.z`, `m1_variables.z`, `m2_control_flow.z`, `m3_types.z`) and narrow regressions pinned to specific fixed bugs. The milestone programs are annotated and double as documentation; keeping them in the suite means they are executed on every run instead of rotting. `Test/sema/` is the larger half of the value: each case asserts a program is *rejected*, and with which message.

Expected-error matching is a substring, not an exact match. That pins which diagnostic fired without turning every wording tweak into a failure. Output comparison strips `\r` so CRLF and LF platforms behave identically.

### Optimisation levels

Every codegen test runs at `-O0`, `-O1`, `-O2`, and `-O3`, and all four must produce byte-identical output. That equivalence is the correctness check on the pass pipeline: a program whose result changes under optimisation was relying on something the optimiser is entitled to change. Narrow it while iterating:

```bash
ZOPT="-O0 -O2" ./Test/run_tests.sh
```

`zc` also re-verifies the module after the pipeline, so a pass that produces malformed IR is caught at compile time rather than becoming a mysterious runtime failure.

## Layout

```
Include/        Headers — Token, AST, Lexer, Parser, Sema, CodeGen
Src/            Implementations + the driver (main.cpp)
Runtime/        C runtime linked into compiled programs (empty until M3)
Test/           run_tests.sh, codegen/ and sema/ suites
docs/Plan.md    Design, milestones, and architectural decisions
```

The pipeline is the conventional one: `Lexer` → `Parser` → `Sema` → `CodeGen` → LLVM IR → object file → `clang` for linking.

Two invariants worth knowing before touching the middle of it:

- **CodeGen dispatches on `Expr::resolvedType`, not on the LLVM type.** Several Z types share one LLVM type — `character` and `int32` are both `i32`, and `string` and `dynamic` will both be pointers. Branching on the LLVM type to make a *semantic* decision is a bug.
- **Sema and CodeGen must agree on scoping.** Both maintain a scope stack and push/pop in exactly the same places. If they diverge, Sema accepts a program that CodeGen resolves to the wrong storage slot.
- **`&&` and `||` are lowered to branches, not to `and`/`or`.** They short-circuit, so the right operand is generated inside its own basic block and the result merges through a `phi`. Any future operator with conditional evaluation (the M3 ternary) needs the same treatment — generating both operands first and selecting afterwards is wrong whenever an operand can have a side effect.

## Known gaps

- `--dump-ast` only handles a handful of node types; control-flow and M3 expression nodes print as `UnknownStmt` / `UnknownExpr`. There is no parser test suite until this is fixed, since it would otherwise lock in incorrect output.
- `Include/Types.h` is empty and `TypeRef` is a flat enum in `AST.h`. It cannot represent parameterised types (`vector<int>`, `tensor<float, 2, 2>`) and will need to become a structured type before M11.
- `int128` values print via truncation to 64 bits.
- The language spec says `character`; the lexer currently accepts `char`.
