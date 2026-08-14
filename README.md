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

## LLVM IR optimisation

`zc` does not implement its own optimiser. It emits deliberately naive IR — every variable is a stack slot, every expression is a fresh instruction — and hands it to LLVM's stock `PassBuilder` pipeline, selected by `-O0`/`-O1`/`-O2`/`-O3`. `-O2` is the default. This section records what those passes actually do to Z programs, measured on the checked-in test suite.

### Measured effect

Counts from `zc -O<n> --emit-llvm` over `Test/codegen/`:

| Program | Level | IR lines | `alloca` | blocks | `load` | calls |
|---|---|---:|---:|---:|---:|---:|
| m1_variables | O0 | 29 | 3 | 1 | 4 | 2 |
| | O1–O3 | 19 | **0** | 1 | **0** | 2 |
| m3_types | O0 | 217 | 22 | 14 | 38 | 30 |
| | O1–O3 | 83 | **0** | 5 | **0** | 24 |
| loop_control | O0 | 304 | 12 | 59 | 34 | 10 |
| | O1–O3 | 34 | **0** | **2** | **0** | 8 |
| short_circuit | O0 | 282 | 3 | 59 | 8 | 22 |
| | O1–O3 | 41 | **0** | **2** | **0** | 17 |
| all_paths_return | O0 | 112 | 4 | 18 | 5 | 14 |
| | O1–O3 | 58 | **0** | 5 | **0** | 7 |
| m2_control_flow | O0 | 591 | 26 | 96 | 60 | 54 |
| | O1 | 136 | 0 | 11 | 0 | 41 |
| | O2–O3 | 188 | 0 | 14 | 0 | 41 |

Three things stand out:

- **Every `alloca` and every `load` disappears at `-O1`.** This is `sroa`/mem2reg promoting stack slots to SSA registers, and it is the single largest transformation applied to Z output. It only works because `CodeGen` places all allocas in the function entry block — the convention exists precisely to keep them promotable.
- **`-O1` already does nearly everything.** For five of the six programs `-O2` and `-O3` are byte-identical to `-O1`. Z programs at this stage are too simple to exercise the extra passes.
- **`-O2` output is sometimes *larger* than `-O1`** (m2_control_flow, 136 → 188 lines). That is not a regression: `-O2` inlines and unrolls more aggressively, trading size for speed. Line count is a proxy for work done, not for quality.

### Passes applied

LLVM 22's default pipelines contain **64 passes at `-O1`, 76 at `-O2`, and 79 at `-O3`**. Grouped by what they do to Z output:

**Memory to registers** — the dominant win
`sroa` (scalar replacement of aggregates / mem2reg), `early-cse` (common subexpression elimination, memory-SSA aware at O2+), `memcpyopt`, `dse` (dead store elimination, O2+), `mldst-motion` (merged load/store motion, O2+), `licm` (loop-invariant code motion), `infer-alignment`, `alignment-from-assumptions`.

**Instruction-level simplification**
`instcombine` (peephole rewriting), `aggressive-instcombine` (O2+), `instsimplify`, `reassociate`, `constraint-elimination` (O2+), `correlated-propagation` (O2+), `div-rem-pairs`, `float2int`, `lower-constant-intrinsics`.

**Constant propagation and dead code**
`ipsccp` (interprocedural sparse conditional constant propagation), `sccp`, `called-value-propagation`, `adce` (aggressive dead code elimination), `bdce` (bit-tracking DCE), `globaldce`, `globalopt`, `constmerge`, `deadargelim`, `elim-avail-extern`.

**Control-flow restructuring**
`simplifycfg` (block merging, branch folding — this is what erases the unreachable blocks `break`/`continue` leave behind), `jump-threading` (O2+), `speculative-execution` (O2+), `chr` (control-height reduction, O3 only), `callsite-splitting` (O3 only).

**Loops**
`loop-rotate`, `loop-simplifycfg`, `loop-deletion`, `loop-unroll`, `loop-unroll-full`, `simple-loop-unswitch`, `indvars` (induction variable simplification), `loop-sink`, `loop-distribute`, `loop-load-elim`.

**Vectorisation**
`loop-vectorize`, `slp-vectorizer` (superword-level parallelism, O2+), `vector-combine`.

**Interprocedural**
`always-inline` and the CGSCC `inliner`, `function-attrs`, `rpo-function-attrs`, `inferattrs`, `argpromotion` (O3 only), `tailcallelim`, `gvn` (global value numbering, O2+).

**What `-O2` adds over `-O1`** (12 passes): `aggressive-instcombine`, `constraint-elimination`, `correlated-propagation`, `dse`, `extra-simple-loop-unswitch-passes`, `gvn`, `jump-threading`, `mldst-motion`, `move-auto-init`, `openmp-opt-cgscc`, `slp-vectorizer`, `speculative-execution`.

**What `-O3` adds over `-O2`** (3 passes): `argpromotion`, `callsite-splitting`, `chr`.

### Worked example

`Test/codegen/loop_control.z` collapses from 304 IR lines and 59 basic blocks to 34 lines and 2 blocks. Every loop has a compile-time-known trip count, so `indvars` + `loop-unroll-full` + `ipsccp` + `instcombine` evaluate them entirely at compile time:

```llvm
define noundef i32 @main() local_unnamed_addr #0 {
entry:
  %0 = tail call i32 (ptr, ...) @printf(ptr @fmt.8, i64 3)
  %1 = tail call i32 (ptr, ...) @printf(ptr @fmt.8, i64 30)
  %2 = tail call i32 (ptr, ...) @printf(ptr @fmt.8, i64 2)
  ...
  ret i32 0
}
```

This doubles as an independent check on the `break`/`continue` lowering: LLVM computed those loop results symbolically, and the constants it produced match the hand-written `.expected` file. Wrong loop-exit edges would have folded to different numbers.

### A known limitation in what we emit

`CodeGen::genFnDecl` gives every function `ExternalLinkage`, including ones only called locally. LLVM therefore cannot delete a function even after inlining it into every caller. In `all_paths_return.z` at `-O2`, all four helpers are fully inlined and constant-folded into `main` — which becomes seven `printf` calls with literal arguments — yet all four definitions survive as unreachable code.

Marking non-`main` functions `internal` would let `globaldce` remove them and would unlock `deadargelim` and `argpromotion`, which currently have nothing to work on. This is a real missed optimisation, not a correctness issue, and it is listed under Known gaps.

## Layout

```
Include/        Headers — Token, AST, Lexer, Parser, Sema, CodeGen
Src/            Implementations + the driver (main.cpp)
Runtime/        Everything linked into or lowered for compiled programs
  MLIR/         The `z` dialect and lowering pipeline (M17a+, opt-in)
Test/           run_tests.sh, codegen/ and sema/ suites
docs/Plan.md    Design, milestones, and architectural decisions
```

`Runtime/` holds what belongs to compiled programs rather than to the compiler: the C runtime (from M3) and the MLIR tensor backend (from M17a). Building the latter requires `-DZ_ENABLE_MLIR=ON`.

The pipeline is the conventional one: `Lexer` → `Parser` → `Sema` → `CodeGen` → LLVM IR → object file → `clang` for linking.

Two invariants worth knowing before touching the middle of it:

- **CodeGen dispatches on `Expr::resolvedType`, not on the LLVM type.** Several Z types share one LLVM type — `character` and `int32` are both `i32`, and `string` and `dynamic` will both be pointers. Branching on the LLVM type to make a *semantic* decision is a bug.
- **Sema and CodeGen must agree on scoping.** Both maintain a scope stack and push/pop in exactly the same places. If they diverge, Sema accepts a program that CodeGen resolves to the wrong storage slot.
- **`&&` and `||` are lowered to branches, not to `and`/`or`.** They short-circuit, so the right operand is generated inside its own basic block and the result merges through a `phi`. Any future operator with conditional evaluation (the M3 ternary) needs the same treatment — generating both operands first and selecting afterwards is wrong whenever an operand can have a side effect.

## Known gaps

- **Every function is emitted with `ExternalLinkage`**, so LLVM cannot delete one even after inlining it everywhere, and the interprocedural passes that depend on knowing all callers (`deadargelim`, `argpromotion`) have nothing to work on. Marking non-`main` functions `internal` in `genFnDecl` would fix this. See the LLVM IR optimisation section for a measured example.
- `--dump-ast` only handles a handful of node types; control-flow and M3 expression nodes print as `UnknownStmt` / `UnknownExpr`. There is no parser test suite until this is fixed, since it would otherwise lock in incorrect output.
- `Include/Types.h` is empty and `TypeRef` is a flat enum in `AST.h`. It cannot represent parameterised types (`vector<int>`, `tensor<float, 2, 2>`) and will need to become a structured type before M11.
- `int128` values print via truncation to 64 bits.
- The language spec says `character`; the lexer currently accepts `char`.
