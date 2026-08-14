# ZCompiler

A compiler for **Z**, a custom multi-paradigm language, written in C++17 against the LLVM C++ API. This is a learning project — the goal is to understand how compilers work by building one end to end, from tokenizer to linked executable.

The full design, milestone breakdown, and architectural decisions live in [docs/Plan.md](docs/Plan.md).

## Status

Milestones **M0–M3** are complete. **M17a** (MLIR foundation) is partially landed.

| Milestone | Scope | State |
|---|---|---|
| M0 | `print` + `return` compiling to a working `.exe` | done |
| M1 | `let`, integer arithmetic, assignment | done |
| M2 | Functions, `if`/`else`, `while`, `for`, `do…along`, `switch`, `break`, `continue` | done |
| M3 | Full type system — `string`, `dynamic`, `null`, ternary, casts | done |
| M4 | Namespaces + `using` import | next |
| M17a | MLIR foundation — `z` dialect, build integration | partial |

M3 delivers the complete primitive set (`int`, `int32`, `int64`, `int128`, `float16`, `float`/`float32`, `double`/`float64`, `bool`, `character`, `string`, `dynamic`), the `null` value, the ternary operator, and both `static_cast<T>` and `dynamic_cast<T>`. `string` is a real runtime type — a heap `ZString` with `+` concatenation and all six comparison operators built in, no import required. `dynamic` boxes any primitive or string with a runtime type tag.

M17a has the `z` dialect compiling and the build integration behind `-DZ_ENABLE_MLIR=ON`; the emitter and lowering pipeline are not yet written. See [docs/Plan.md](docs/Plan.md).

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

Three suites, discovered by file layout — dropping in a `.z` file and its expected-output companion is all it takes to add a case:

| Suite | Files | Check |
|---|---|---|
| `Test/parser/` | `*.z` + `*.expected-ast` | `--dump-ast` output must match exactly — pins precedence, associativity and node shape |
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

`zc` does not implement its own optimiser. It emits deliberately naive IR — every variable is a stack slot, every expression a fresh instruction — and hands it to LLVM's stock `PassBuilder` pipeline, selected by `-O0`/`-O1`/`-O2`/`-O3`. `-O2` is the default.

Verified to fire on Z code today: **mem2reg (promotion of memory to registers)**, **constant folding**, **sparse conditional constant propagation**, **function inlining**, **dead code elimination**, **CFG simplification / branch folding**, **full loop unrolling**, **loop-invariant code motion**, **induction-variable simplification and loop deletion via scalar evolution**, **common subexpression elimination**, **reassociation**, **strength reduction**, and **tail call optimisation**.

The rest of this section is the measurement behind that claim: what changes, which pass does it, and the reproducible evidence for each.

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

### Techniques applied, and evidence each one fires

LLVM 22's default pipelines contain **64 passes at `-O1`, 76 at `-O2`, 79 at `-O3`**. The table below names the classical optimisation each relevant pass implements, and — because a list of pass names proves nothing on its own — the observed effect on this project's own code. Every "evidence" entry below was reproduced with `zc --emit-llvm`, `opt -pass-remarks`, or both.

| # | Technique | LLVM pass | Evidence in this project |
|---|---|---|---|
| 1 | **Promotion of memory to registers** (mem2reg) | `sroa` | `m3_types.z`: 22 `alloca` + 38 `load` at `-O0` → **0 and 0** at `-O1`. Works only because `CodeGen` emits allocas in the entry block. |
| 2 | **Constant folding** | `instcombine`, `instsimplify` | `strength(5)`, `cse(3,4)` fold to literals at their call sites. |
| 3 | **Constant propagation** (sparse conditional, interprocedural) | `sccp`, `ipsccp`, `called-value-propagation` | `all_paths_return.z` `main` becomes 7 `printf` calls with literal arguments — every helper's return value propagated through. |
| 4 | **Function inlining** | `inliner`, `always-inline` | `opt -pass-remarks` on `m2_control_flow.z` reports `day_name` inlined ×4, `is_even` ×3, plus `min`, `max`, `factorial`, `abs`. |
| 5 | **Dead code elimination** | `adce`, `bdce`, `dce` | `loop_control.z`: 59 basic blocks → **2**. Includes the unreachable blocks `break`/`continue` deliberately leave behind. |
| 6 | **Control-flow graph simplification** (branch folding, block merging) | `simplifycfg` | `short_circuit.z`: 59 blocks → 2. The `and.rhs`/`or.rhs` blocks collapse once the branch condition is known. |
| 7 | **Loop unrolling** (full) | `loop-unroll`, `loop-unroll-full` | `opt -pass-remarks` on `loop_control.z` reports a loop `unrolled`; all fixed-trip-count loops vanish. |
| 8 | **Loop-invariant code motion** | `licm` | Probe `licm(n, k)`: `k * 8` inside the loop is hoisted out entirely. |
| 9 | **Induction-variable simplification / scalar evolution** | `indvars`, `loop-deletion` | Same probe: the accumulation loop is replaced by the closed form `(k * n) << 3` and **deleted**. See below. |
| 10 | **Common subexpression elimination** | `early-cse`, `gvn` (O2+) | Probe `a*b + a*b` → the product is computed once. |
| 11 | **Reassociation** | `reassociate` | Same probe: `a*b + a*b` is refactored to `2*a*b` before strength reduction. |
| 12 | **Strength reduction** | `instcombine` | `x * 8` → `shl i64 %x, 3`. `a*b*2` → `shl i64 %a, 1` then multiply. |
| 13 | **Tail call optimisation** | `tailcallelim` | 41 `tail call` markers in `m2_control_flow.z` at `-O2`. |
| 14 | **Dead store elimination** | `dse` (O2+) | In the pipeline; **no observed effect** on Z output — `sroa` removes every store to a stack slot first, leaving nothing for it to do. |
| 15 | **Global value numbering** | `gvn` (O2+) | In the pipeline; **no observed effect** beyond what `early-cse` already achieves at this program size. |
| 16 | **Jump threading** | `jump-threading` (O2+) | In the pipeline; **no measured effect** on the current suite. |
| 17 | **Vectorisation** (loop and superword-level) | `loop-vectorize`, `slp-vectorizer` (O2+) | In the pipeline; **does not fire** — zero vector types across all 9 programs at `-O2` and `-O3`. Nothing to vectorise until `structures` (M11) and `tensor` (M17b) introduce aggregates. |
| 18 | **Interprocedural argument optimisation** | `deadargelim`, `argpromotion` (O3) | In the pipeline; **cannot fire** — see the linkage limitation below. |

Entries 14–18 are listed because they are genuinely in the pipeline and will matter as the language grows, but nothing in the current test suite demonstrates them. Rows 1–13 are the ones with reproducible evidence today.

**What `-O2` adds over `-O1`** (12 passes): `aggressive-instcombine`, `constraint-elimination`, `correlated-propagation`, `dse`, `extra-simple-loop-unswitch-passes`, `gvn`, `jump-threading`, `mldst-motion`, `move-auto-init`, `openmp-opt-cgscc`, `slp-vectorizer`, `speculative-execution`.

**What `-O3` adds over `-O2`** (3 passes): `argpromotion`, `callsite-splitting`, `chr` (control-height reduction).

### Two worked examples

**Whole-loop constant folding.** `Test/codegen/loop_control.z` collapses from 304 IR lines and 59 basic blocks to 34 lines and 2 blocks. Every loop has a compile-time-known trip count, so unrolling plus constant propagation evaluates them entirely at compile time:

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

This doubles as an independent check on the `break`/`continue` lowering: LLVM computed those loop results symbolically, and the constants match the hand-written `.expected` file. Wrong loop-exit edges would have folded to different numbers.

**Loop elimination via scalar evolution.** A loop accumulating a loop-invariant product:

```z
fn licm(n: int, k: int) -> int {
    let acc: int = 0
    let i: int = 0
    while (i < n) {
        acc = acc + k * 8
        i = i + 1
    }
    return acc
}
```

`-O2` removes the loop entirely, replacing `n` iterations with a closed-form expression — LICM hoists `k * 8`, scalar evolution recognises the accumulation, `loop-deletion` drops the now-empty loop, and `instcombine` reduces `* 8` to a shift:

```llvm
define range(i64 0, -7) i64 @licm(i64 %n, i64 %k) local_unnamed_addr #1 {
entry:
  %0 = icmp sgt i64 %n, 0
  %1 = mul i64 %k, %n
  %2 = shl i64 %1, 3
  %acc.0.lcssa = select i1 %0, i64 %2, i64 0
  ret i64 %acc.0.lcssa
}
```

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
- `Include/Types.h` is empty and `TypeRef` is a flat enum in `AST.h`. It cannot represent parameterised types (`vector<int>`, `tensor<float, 2, 2>`) and will need to become a structured type before M11.
- `int128` values print via truncation to 64 bits.
- **Nothing is ever freed.** `z_gc_alloc` is a `malloc` wrapper, so every string concatenation and every boxed `dynamic` leaks. The `ZGCHeader` is already in place on both types so the M14 collector needs no layout change, but until then long-running programs grow without bound.
- `z_string_cstr` relies on every `ZString` being allocated one byte longer than its length, with that byte left zero. This ends when M6 adds slices that alias a parent buffer — at that point the function has to copy.
- Strings are byte sequences, not Unicode-aware. Comparison is byte-wise lexicographic, `character` holds a code point but `print` emits it with `%c`, so non-ASCII code points do not round-trip yet.
