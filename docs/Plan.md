# ZCompiler — Build a Compiler with C++ and LLVM

## Context

This is a **learning project** to understand how compilers work by building one from scratch. The "Z" language is a custom multi-paradigm language (imperative + OO + functional foundations). The compiler is written in C++17, targeting LLVM for IR generation and machine code output.

Feature scope:
- **Types**: `int` (platform-native width, 64-bit on x64), `int32`, `int64`, `int128`, `float16` (16-bit IEEE 754 half precision — storage/ML type, no arithmetic literals), `float` / `float32` (32-bit IEEE 754 — both keywords resolve to the same type), `double` / `float64` (64-bit IEEE 754 — both keywords resolve to the same type), `bool`, `character`, `string` (always a keyword — no import required; `+` concatenation and `==`/`!=`/`<`/`<=`/`>`/`>=` comparisons are built-in operators; all other operations are free functions provided by the `string` library, M6), `null` (the absence of a value — assignable to any pointer/reference type; comparing a non-null pointer to `null` is always valid), `dynamic` (runtime-typed value — holds any primitive or string; type tag checked at runtime). (`date` and `time` are not core primitives — they live in the `datetime` library with constructor-call syntax to avoid lexer ambiguity.)
- **Control flow**: `if (cond) { ... }`, `while (cond) { ... }`, `do { ... } along (cond)`, `for (init; cond; step) { ... }`, `switch (expr) { case <lit>: { ... } ... default: { ... } }` — parentheses around the condition are **required** (C-style). `do ... along` is a post-test loop — body executes before the first condition check. `break` exits the nearest enclosing loop or `switch` immediately. `continue` skips to the next iteration of the nearest enclosing loop (`while`, `for`, `do...along`); it is a Sema error inside a `switch` that is not itself inside a loop.
- **Expressions**: ternary `cond ? a : b` — evaluates `cond`; if truthy returns `a`, otherwise `b`. Both branches must have compatible types. Lower precedence than `||`, higher than assignment.
- **Standard libraries** (imported with C#-style `using <name>`):
  - `structures` — `array<T>` (fixed-size GC-managed sequence), `vector<T>` (dynamic array, doubling growth), `list<T>` (doubly-linked list), `stack<T>`, `queue<T>`, `heap<T>` (min-heap / priority queue), `bstree<T>` (binary search tree), `map<K, V>` (ordered map — red-black tree), `unordered_map<K, V>` (hash map). All methods: `push`, `pop`, `top`, `size`, `empty`, plus container-specific accessors.
  - `math` — `sqrt`, `pow`, `sin`, `cos`, `tan`, `log`, `exp`, `abs`, `floor`, `ceil`, `min`, `max`; constants `PI`, `E`
  - `datetime` — `date(year, month, day)` and `time(hour, minute, second)` constructor calls, accessors (`.year()`, `.hour()`, etc.), arithmetic (`add_days`, `diff`, `add_seconds`), formatting (`.format(fmt)`, `.iso()`), and current-time queries (`today()`, `now()`)
  - `algorithms` — sorting (`bubble_sort`, `insertion_sort`, `merge_sort`, `quick_sort`, `heap_sort`), searching (`linear_search`, `binary_search`), BST traversals (`in_order`, `pre_order`, `post_order`, `level_order`). Operates on `structures` containers; depends on `structures` only.
  - `tensor` — dense numeric tensors `tensor<T>` (dynamic shape) or `tensor<T, d0, d1, ...>` (static shape) with elementwise ops, broadcasting, reductions, linear algebra, **reverse-mode autograd** (`.backward()`, `.grad()`, `with no_grad() { ... }`), and **GPU execution** via CUDA (`.to(CUDA)`)
  - `regression` — statistical regression models: linear (OLS), multiple linear, polynomial, ridge (L2), lasso (L1), elastic net, and logistic regression. Models expose a common `.fit(X, y)` / `.predict(X)` / `.score(X, y)` interface. Self-hosted in `stdlib/regression.z` on top of the `tensor` core — the sklearn-on-numpy pattern, no parallel linalg stack.
- **Namespaces**: `namespace NAME { fn ... class ... }` groups declarations under a named scope. `using NAME` imports every symbol from that namespace into the current file (C#-style whole-namespace import). Qualified access `NAME.symbol(args)` works without `using`. Built-in libraries (`math`, `structures`, etc.) are namespaces defined in stdlib files — the same mechanism as user-defined ones.
- **Imports**: `using NAME` — appears at the top of a file, one per line, before any `fn`/`class`/`namespace` declaration. Unimported namespaces are invisible — referencing `list<int>` without `using structures` is a compile error. Duplicate `using` lines are an error.
- **OOP**: classes with fields/methods/constructors, single inheritance, interfaces, method overriding (virtual dispatch via vtables), method/function overloading (via name mangling)
- **Constructor syntax**: `ClassName(params): super(args), field1 = expr, field2 = expr { body }` — constructor is named after the class, optional base-class init and field initializer list before the body

---

## Prerequisite: LLVM + MLIR Dev Libraries

**Current toolchain (as built):** MSYS2 UCRT64, LLVM **21.1.8** (`mingw-w64-ucrt-x86_64-llvm`), CMake config at `C:\msys64\ucrt64\lib\cmake\llvm`. `zc` builds and links against this today with the Ninja generator. This is what `build/CMakeCache.txt` must point at — if the cache references a stale source path, delete `build/` and re-run `cmake -S . -B build -G Ninja`.

**MLIR is required from M17 onward** (see the tensor-lowering decision below). MLIR is *not* currently installed and is **version-locked to LLVM** — MLIR N links against LLVM N's libraries, so the two must be upgraded together:

```bash
# MSYS2 UCRT64 shell — installs MLIR and pulls LLVM libs to the matching version
pacman -S mingw-w64-ucrt-x86_64-mlir
```

Concrete state at time of writing: the repo has LLVM **21.1.8** installed; the MSYS2 `mlir` package is **22.1.7** and depends on `llvm-libs`, so installing it **upgrades LLVM 21 → 22**. Budget for that: ~1.2 GB installed, plus one round of fixing whatever LLVM 22 API churn breaks in `CodeGen.cpp` / `main.cpp`. Do this upgrade as a standalone step *before* M17 starts, not inside it — a version bump tangled with new tensor work makes both undebuggable.

After install, CMake picks MLIR up via `find_package(MLIR REQUIRED CONFIG)`; `MLIR_DIR` lands at `C:\msys64\ucrt64\lib\cmake\mlir` and `MLIRConfig.cmake` re-exports `LLVM_DIR`, so the existing LLVM lookup keeps working.

**MLIR is opt-in in the build.** Gate it behind `-DZ_ENABLE_MLIR=ON` so that M0–M16 keep building on an LLVM-only install. Contributors working below M17 never need the 1.2 GB dependency.

---

## Project Structure

The repo uses PascalCase top-level directories and a split `Include/` + `Src/` layout. Paths written lowercase elsewhere in this document (`src/CodeGen.cpp`, `runtime/zstring.c`, `tests/codegen/`) refer to these directories.

```
d:\Projects\ZLanguage\
├── CMakeLists.txt
├── Include/              # Public headers — every .h lives here, not next to the .cpp
│   ├── Token.h           # TokenType enum + Token struct
│   ├── Lexer.h
│   ├── AST.h             # All AST node classes + TypeRef enum
│   ├── Parser.h
│   ├── Types.h           # Z type representation (currently empty — see M3)
│   ├── Sema.h            # Semantic analysis (type checking, symbol table)
│   ├── CodeGen.h         # LLVM IR generation
│   └── Mangler.h         # Name mangling for overloads and methods (empty until M16)
├── Src/
│   ├── main.cpp          # Driver: reads .z file, runs pipeline
│   ├── Lexer.cpp
│   ├── Parser.cpp
│   ├── Sema.cpp
│   ├── CodeGen.cpp
│   ├── Mangler.cpp
│   └── MLIR/             # M17+ only, built when -DZ_ENABLE_MLIR=ON
│       ├── ZDialect.td       # TableGen definition of the `z` dialect
│       ├── ZDialect.cpp      # Generated-code glue + custom op verifiers
│       ├── TensorEmitter.cpp # Z AST tensor exprs -> `z` dialect ops
│       └── Pipeline.cpp      # z -> linalg -> bufferize -> scf -> llvm pass pipeline
├── Runtime/              # C runtime linked into every compiled program
│   ├── Headers/          # zstring.h, zdynamic.h, zgc.h, zruntime.h, ztensor.h
│   └── Main/             # zstring.c, zdynamic.c, zgc.c, zruntime.c, ztensor.c
├── Test/
│   ├── run_tests.sh      # Suite runner — discovers tests by file layout
│   ├── codegen/          # .z + .expected — compile, run, compare stdout.
│   │                     #   Holds the per-milestone programs (m0_hello.z,
│   │                     #   m2_control_flow.z, …) and narrow bug regressions.
│   │                     #   Each runs at -O0/-O1/-O2/-O3; all must agree.
│   └── sema/             # .z + .expected-error — must fail to compile
└── build/                # Ninja build dir — not committed
```

There is no separate `Examples/` directory: the milestone programs live in `Test/codegen` so they are executed on every run rather than rotting. A program that documents a milestone and a program that regression-tests it are the same artifact.

---

## Z Language Syntax

C-like base, newline-terminated statements (no semicolons), `let` for declarations, explicit types with `:`.

### Imports — `using`

Libraries are imported at the top of a file with `using <name>` (C#-style whole-namespace import). Each `using` line brings every public symbol from that library into the current file's scope.

```z
using structures
using math

fn main() -> int {
    let xs: vector<int> = vector<int>()
    xs.push(4)
    print(sqrt(16.0))       # from math
    return 0
}
```

Rules:
- `using` declarations must appear before any `fn`, `class`, or `interface` declaration in the file.
- Each library is imported at most once per file; duplicate `using` lines are an error.
- Names not imported are undefined — e.g. using `list<T>` without `using structures` is a "use of undeclared type" error.
- Available libraries in v1: `structures`, `math`, `tensor`, `regression`. Libraries are recognized by the compiler (there is no user-defined library support yet).

### Basic program
```z
using structures

fn factorial(n: int) -> int {
    if (n <= 1) { return 1 }
    return n * factorial(n - 1)
}

fn classify(n: int) -> int {
    switch (n) {
        case 0:  { return 0 }                     # zero
        case 1:  { return 1 }                     # unit
        case 2:  { return 2 }                     # smallest prime
        default: { return -1 }
    }
}

fn main() -> int {
    let nums: array<int> = array<int>([1, 2, 3, 4, 5])
    for (i: int = 0; i < nums.size(); i = i + 1) {
        print(nums.get(i))
    }
    print(factorial(5))
    print(classify(2))
    return 0
}
```

### Types and containers
```z
using structures

fn main() -> int {
    let a: int32 = 100
    let b: int64 = 10000000000
    let c: int128 = 170141183460469231731687303715884105727
    let pi: float = 3.14
    let e: double = 2.718281828459045
    let ch: character = 'Z'

    let xs: vector<int> = vector<int>()      # dynamic array — random access, O(1) push/pop at end
    xs.push(1)
    xs.push(2)
    xs.push(3)
    print(xs.size())
    print(xs.get(1))    # 2

    let names: list<string> = list<string>() # doubly-linked list — O(1) insert at either end
    names.push_back("alice")
    names.push_front("bob")
    print(names.front()) # "bob"

    let scores: map<string, int> = map<string, int>()      # ordered map (red-black tree)
    scores.insert("alice", 90)
    scores.insert("bob", 75)
    print(scores.get("alice"))   # 90 — and iteration would yield keys in alphabetical order

    let counts: unordered_map<string, int> = unordered_map<string, int>()  # hash map
    counts.insert("hits", 0)
    counts.insert("hits", counts.get("hits") + 1)
    print(counts.get("hits"))    # 1

    let s: stack<int> = stack<int>()
    s.push(10)
    s.push(20)
    print(s.top())      # 20
    s.pop()

    let q: queue<string> = queue<string>()
    q.push("first")
    q.push("second")
    print(q.front())    # "first"

    let h: heap<int> = heap<int>()   # min-heap / priority queue
    h.push(5)
    h.push(1)
    h.push(3)
    print(h.top())      # 1

    return 0
}
```

### math library example
```z
using math

fn main() -> int {
    let r: double = 2.0
    let area: double = PI * pow(r, 2.0)
    print(area)
    print(sqrt(2.0))
    print(max(3, 7))
    return 0
}
```

### tensor library example
```z
using tensor

fn main() -> int {
    # Static-shape form — shape known at compile time
    # float32 is the default tensor dtype (matches PyTorch default, 2x faster than double on GPU)
    let v: tensor<float, 3> = tensor<float, 3>([1.0, 2.0, 3.0])
    let m: tensor<float, 2, 2> = tensor<float, 2, 2>([[1.0, 2.0], [3.0, 4.0]])
    print(v.sum())              # 6.0
    print(m.shape())            # [2, 2]

    let w: tensor<float, 3> = v + v        # elementwise
    let s: tensor<float, 3> = v * 2.0      # scalar broadcast
    print(w.get(0))                         # 2.0

    # Dynamic-shape form — float32 training
    let x: tensor<float> = tensor<float>.randn([128, 64]).requires_grad(true)
    let W: tensor<float> = tensor<float>.randn([64, 10]).requires_grad(true)
    let y: tensor<float> = x.matmul(W).relu()
    let loss: tensor<float> = y.sum()

    # Autograd — populate W.grad() and x.grad()
    loss.backward()
    print(W.grad().shape())

    # Dtype cast — float16 for fast GPU inference (half memory, 2x throughput on tensor cores)
    let x_half: tensor<float16> = x.half()     # shorthand for .to(float16)
    let W_half: tensor<float16> = W.half()
    print(x_half.dtype())                       # float16

    # GPU execution — move to CUDA, run in float16, move result back as float32
    let xg: tensor<float16> = x.half().to(CUDA)
    let Wg: tensor<float16> = W.half().to(CUDA)
    let yg: tensor<float16> = xg.matmul(Wg)
    let y_cpu: tensor<float> = yg.to(CPU).to(float)   # back to float32

    # Inference path — suppress tape recording
    with no_grad() {
        let pred: tensor<float> = x.matmul(W)
        print(pred.shape())
    }

    return 0
}
```

### regression library example
```z
using tensor
using regression

fn main() -> int {
    # Regression uses float64 (double) for numerical stability —
    # matrix inversion and OLS normal equations lose precision in float32.
    let X: tensor<double> = tensor<double>([[1.0], [2.0], [3.0], [4.0]])
    let y: tensor<double> = tensor<double>([2.1, 3.9, 6.2, 8.1])

    # Ordinary least squares
    let lin: LinearRegression = LinearRegression()
    lin.fit(X, y)
    print(lin.coef())           # slope(s)
    print(lin.intercept())
    print(lin.score(X, y))      # R^2

    # Polynomial regression of degree 3
    let poly: PolynomialRegression = PolynomialRegression(3)
    poly.fit(X, y)
    print(poly.predict(tensor<double>([[5.0]])))

    # Ridge with L2 penalty
    let ridge: RidgeRegression = RidgeRegression(0.1)
    ridge.fit(X, y)

    # Logistic regression for binary classification
    let Xc: tensor<double> = tensor<double>([[0.0], [1.0], [2.0], [3.0]])
    let yc: tensor<double> = tensor<double>([0.0, 0.0, 1.0, 1.0])
    let log: LogisticRegression = LogisticRegression()
    log.fit(Xc, yc)
    print(log.predict_proba(tensor<double>([[1.5]])))

    return 0
}
```

### OOP example — classes, interfaces, inheritance, overriding, overloading
```z
interface Drawable {
    fn draw() -> void
    fn area() -> double
}

class Shape : Drawable {
    let name: string

    Shape(n: string): name = n { }

    ~Shape() {
        print("shape destroyed")     # called by the GC when this object is collected
    }

    virtual fn draw() -> void {
        print("drawing shape")
    }

    virtual fn area() -> double {
        return 0.0
    }
}

class Circle : Shape {
    let radius: double

    Circle(r: double): super("circle"), radius = r { }

    ~Circle() {
        print("circle destroyed")    # runs first, then ~Shape() runs (child-to-parent order)
    }

    fn draw() override -> void {
        print("drawing circle")
    }

    fn area() override -> double {
        return 3.14159 * self.radius * self.radius
    }
}

# Overloading — same name, different signatures
fn max(a: int, b: int) -> int {
    if (a > b) { return a }
    return b
}
fn max(a: double, b: double) -> double {
    if (a > b) { return a }
    return b
}

fn main() -> int {
    let c: Circle = new Circle(5.0)
    let s: Shape = c           # upcast — polymorphism
    s.draw()                   # dispatches to Circle.draw via vtable
    print(s.area())            # dispatches to Circle.area
    print(max(1, 2))
    print(max(1.5, 2.5))
    return 0
}
```

### Grammar (Simplified EBNF)
```
program       = { using_decl } { top_decl }
using_decl    = "using" IDENT newline
top_decl      = function_def | class_def | interface_def | namespace_decl
namespace_decl = "namespace" IDENT "{" { top_decl } "}"
function_def  = "fn" IDENT "(" param_list ")" "->" type block
class_def     = "class" IDENT [ ":" IDENT { "," IDENT } ] "{" { class_member } "}"
interface_def = "interface" IDENT "{" { method_sig } "}"
class_member  = field_decl | ctor_decl | dtor_decl | method_decl
field_decl    = "let" IDENT ":" type
ctor_decl     = IDENT "(" param_list ")" [ ":" init_list ] block
dtor_decl     = "~" IDENT "(" ")" block
init_list     = init_item { "," init_item }
init_item     = "super" "(" args ")" | IDENT "=" expr
method_decl   = [ "virtual" ] "fn" IDENT "(" param_list ")" [ "override" ] "->" type block
method_sig    = "fn" IDENT "(" param_list ")" "->" type

if_stmt       = "if" "(" expr ")" block [ "else" block ]
while_stmt    = "while" "(" expr ")" block
do_along_stmt = "do" block "along" "(" expr ")"
for_stmt      = "for" "(" ( let_stmt | assign_stmt ) ";" expr ";" assign_stmt ")" block
switch_stmt   = "switch" "(" expr ")" "{" { case_arm } [ default_arm ] "}"
case_arm      = "case" const_expr ":" block
default_arm   = "default" ":" block
const_expr    = INT_LIT | CHAR_LIT | "true" | "false"             # only constants in case arms
break_stmt    = "break"
continue_stmt = "continue"
stmt          = let_stmt | assign_stmt | if_stmt | while_stmt | do_along_stmt
              | for_stmt | switch_stmt | return_stmt | expr_stmt
              | break_stmt | continue_stmt

primitive     = "int" | "int32" | "int64" | "int128"
              | "float16"
              | "float" | "float32"        # synonyms — same 32-bit IEEE 754 type
              | "double" | "float64"        # synonyms — same 64-bit IEEE 754 type
              | "bool" | "character" | "string"   # string is always available — no import needed
              | "dynamic"
container     = "array" "<" type ">"                                   # from `using structures`
              | "vector" "<" type ">" | "list" "<" type ">"
              | "stack" "<" type ">" | "queue" "<" type ">"
              | "heap" "<" type ">"  | "bstree" "<" type ">"
              | "map" "<" type "," type ">"
              | "unordered_map" "<" type "," type ">"
tensor_type   = "tensor" "<" type { "," INT_LIT } ">"                 # from `using tensor`
type          = primitive | container | tensor_type | IDENT
null_lit      = "null"                                                 # typed as the null pointer
ternary_expr  = or_expr [ "?" expr ":" expr ]       # lower than ||, higher than assignment
cast_expr     = "static_cast"  "<" type ">" "(" expr ")"
dyn_cast_expr = "dynamic_cast" "<" type ">" "(" expr ")"
new_expr      = "new" IDENT "(" args ")" | container "(" ")"
member_access = primary "." IDENT [ "(" args ")" ]
ns_access     = IDENT { "." IDENT } "." IDENT [ "(" args ")" ]   # dotted namespace — A.B.fn(args)
primary       = ... | null_lit | cast_expr | new_expr | "self" | member_access
```

- `if`, `while`, `for`, `switch`, `along` conditions must be wrapped in parentheses. `do` takes no condition — it opens the body block; `along` closes it with the post-test condition.
- `switch` arms each take a single constant expression (integer / character / boolean literal — no ranges, no expressions, no fall-through). Each `case` body is a brace block; control exits the `switch` at end of the matched block. `default` is optional and matches any value not covered by a `case`. Duplicate case values are a Sema error.
- Constructor is named after the class. The initializer list (after `:`) can contain a single `super(args)` call and zero or more `field = expr` assignments, separated by commas. These run before the body.
- `virtual` is a **prefix** modifier (`virtual fn draw() -> void { ... }`) marking a method as dispatchable through a vtable. `override` is a **suffix** modifier placed after the parameter list (`fn draw() override -> void { ... }`) declaring that this method overrides a parent's virtual method. The asymmetric placement matches user intent: `virtual` describes the declaration shape; `override` is a check on the signature you just wrote, like a postcondition. Methods declared in an interface are implicitly virtual in implementers.
- A class may declare at most one destructor: `~ClassName() { ... }`, no parameters, no return type. Destructors run as **finalizers** — the tracing GC calls them when the object is collected, not at a deterministic scope exit. In an inheritance chain the child's destructor runs before the parent's (`~Circle` → `~Shape`); the compiler chains the parent call automatically, users do not write `super()`. Throwing, calling `new`, or re-reviving `self` from inside a destructor is undefined behavior in v1 — keep finalizers to releasing non-memory resources (file handles, sockets) or logging.

---

## Cross-Cutting Decisions To Resolve Before Coding

Five foundational questions must be answered before their respective milestones — each shapes every API downstream and is very painful to change once code exists.

### Memory management (blocks M12 / Classes)

`new ClassName(args)` needs a lifetime story before any user-visible code in M12 lands; the plan handles this by splitting OOP into two milestones — M12 ships classes that leak, M13 adds the collector that retroactively makes them not leak. The chosen model is a **tracing garbage collector** for every heap value (class instances, tensors, strings, container elements) — one unified model, no mixing. Alternatives considered and rejected:

- **Manual `delete`** — cheap to implement, pushes correctness onto the user. Ugly for a learning-focused language and makes the stdlib's ergonomic methods (`.fit`, `.push`) land-mined with ownership questions.
- **RAII with scope-bound destructors** — needs a full move/copy-semantics story (lvalue/rvalue references, etc.) that more than doubles the type system.
- **Reference counting everywhere** — simpler codegen than a tracing GC, but cannot collect cycles (tensor computation graphs, parent↔child class references, doubly-linked lists all leak silently) and pays per-op overhead on every assignment.
- **Tracing GC** — the chosen model. One collector owns every heap object, cycles collect cleanly, user code writes `new` and forgets. Destructors (`~ClassName()`) become finalizers invoked by the collector at reclaim time. The cost is a dedicated runtime milestone (M13) and a stop-the-world pause model in v1.

Recommendation: **tracing GC, mark-and-sweep, stop-the-world, non-moving, shadow-stack roots** for v1. Pays the one-time implementation cost in M13 but gives every later milestone a "just allocate, don't think about lifetimes" programming model — crucial for the tensor / autograd / regression work where object graphs naturally form cycles.

### Error handling (blocks M6 / libraries)

`.predict` "raises a runtime error" if called before `.fit`, but the plan does not say *how*. This decision shapes every library API in M6+ — don't defer it. Options:

- **`abort()` with a message** — simplest, unrecoverable. Good enough for assertion-style "you used the API wrong" errors.
- **Exceptions (`throw` / `try` / `catch`)** — familiar but adds unwinding machinery, ABI complications with LLVM, and interacts with whatever memory model you pick above.
- **`Result<T, E>` return type** — forces error handling at the call site, no unwinding needed, but requires pattern matching or a destructuring syntax.
- **Return sentinel + global `errno`-style** — avoid. Burns you instantly in concurrent code and is ugly in every API.

Recommendation: ship **`abort()` with a message** for v1 (M6–M16) and revisit for M17. Any user-facing library that can fail on recoverable input (file not found, parse error) can return a `Result<T, E>` once added — but internal precondition violations (`.predict` before `.fit`, shape mismatch on a static tensor) are always `abort()` and don't need exceptions.

### Stdlib hosting model (blocks M10 / `structures`)

How does a library like `structures` or `regression` actually enter the compiler? Two options, and mixing them is the trap:

- **Compiler-synthesized** — Sema sees `using structures` and constructs `ClassDecl` / `FnDecl` nodes in C++ directly. No Z source file. Fast to bootstrap but the stdlib is invisible to users (can't grep, can't debug, can't step through), and it creates a second class system that silently diverges from the user-facing one.
- **Self-hosted** — the stdlib lives in `stdlib/<name>.z` as ordinary Z source. `using structures` triggers Sema to parse and type-check that file through the normal pipeline. The compiler special-cases nothing except types the language *cannot yet express* (e.g. `list<T>` before real generics — that one C-backed container is a temporary ceiling, not a pattern).

Recommendation: **self-hosted by default, compiler-synthesized only as a documented escape hatch.** `math` (M7), `regression` (M18), and every future library must live in `stdlib/*.z`. The single exception in v1 is the `structures` generic containers, which sit on a C runtime (`runtime/zruntime.c`) until real generics exist — flagged as a known ceiling, not a blueprint. If you find yourself synthesizing a second `ClassDecl` from C++, stop — that's the signal the language itself is missing something and the stdlib is papering over it.

### Generics strategy (blocks M6 `string`, M11 `structures`, M17 `tensor`)

The grammar already uses `vector<int>`, `map<string, int>`, and `tensor<float, 2, 2>` throughout — but the plan never said how parameterized types work. This decision shapes the type checker, the codegen, and every container API.

Three options, with trade-offs:

- **Monomorphization (C++ templates)** — the compiler generates one specialized copy of each generic function/type per concrete type argument. Best performance, no boxing. Cost: multi-pass compilation, exponential code-size risk, significant frontend complexity. Right long-term, wrong for a v1 learning compiler.
- **Type-erased with `void*` (C approach)** — all container operations work on `void*`; element size is passed as a runtime parameter. The C runtime (`zruntime.c`) already does this for `structures`. Works today, but the type checker still needs to track `vector<int>` vs `vector<double>` to catch mixing errors at compile time.
- **Compiler-hardcoded built-in generics only** — the compiler knows the set of generic type constructors (`vector<T>`, `map<K,V>`, `tensor<T,...>`) as special forms. The type checker validates and tracks type arguments. Codegen routes to type-erased C runtime functions, passing element sizes. User-defined generic functions or classes are not supported in v1.

Recommendation: **compiler-hardcoded built-in generics only for v1.** User-defined generics are deferred to post-v1. This requires:

- A `GenericType` AST node: `{ string name; vector<TypeNode*> args; }` representing e.g. `vector<int>`, `map<string, double>`, `tensor<float, 2, 3>`.
- The `type` grammar rule already accommodates `container` and `tensor_type` productions that parse `<T>` syntax. `string` is a plain `primitive` — no type arguments.
- Sema validates that each type argument is itself a known type (recursive). A `vector<UnknownType>` is a Sema error.
- Codegen passes element byte-size to the C runtime (`z_vector_push(vec, &elem, sizeof(int))`) — the element type is erased at runtime, validated at compile time.
- The set of recognized generic constructors is: `array`, `vector`, `list`, `stack`, `queue`, `heap`, `bstree`, `map`, `unordered_map` (from `structures`) and `tensor` (from `tensor`). Any other name in `<T>` position (e.g. `mygenerics<int>`) is a Sema error in v1.

### Tensor lowering strategy (blocks M17 / `tensor`)

Every other library in this plan lowers to either inline LLVM IR or a call into a hand-written C runtime function. `tensor` is the first library where that is the wrong answer, and the choice has to be made before a single line of `ztensor.c` exists — because it determines whether M20/M21 (GPU), M22 (static-shape fast path), M32 (AMP), and M33 (JIT export) are four separate projects or four configurations of one pipeline.

- **Hand-written C runtime loops** — `z_tensor_add(a, b)` walks strides in a C `for` nest, `matmul` is a triple loop or a cuBLAS call. Cheapest to start, and it is what an earlier draft of this plan assumed. The cost compounds: every op needs a CPU loop *and* a `.cu` kernel *and* a `.hip.cpp` kernel, `x.matmul(W).relu()` can never fuse because the ops are opaque function calls, and the static-shape fast path (M22) means writing a *third* implementation of every op in raw LLVM IR.
- **Hand-written LLVM IR loop nests in CodeGen** — good for static shapes, but LLVM IR has no notion of a tensor, a shape, or a reduction. Broadcasting, bufferization, and tiling all become bespoke C++ in `CodeGen.cpp`. This is the path that ends with an ad-hoc, undocumented reimplementation of MLIR inside the compiler.
- **MLIR dialects** — the chosen model. Tensor expressions lower into MLIR's `linalg`/`tensor` dialects, then through a standard progressive-lowering pipeline to the `llvm` dialect and out to LLVM IR that joins the module the rest of the compiler already emits. Fusion, tiling, vectorization, and bufferization are existing passes, not new code. GPU support becomes a *different lowering path from the same source ops* (`linalg` → `gpu` → `nvvm`/`rocdl`) rather than a parallel kernel library.

Recommendation: **MLIR, entered at M17a as its own milestone, with `linalg` on `tensor` as the entry dialect.** Rationale:

- **The op count is the argument.** Roughly 40 tensor ops × {CPU, CUDA, ROCm, static-shape} = ~160 hand-written implementations under the C-runtime model. Under MLIR most ops are one `linalg.generic` with a different body region, and the four targets are four pass pipelines over the same ops.
- **It collapses three later milestones.** M22 (static-shape fast path) stops being a codegen project and becomes pass-pipeline configuration. M33 (JIT / graph export) gets a serialization format for free — MLIR bytecode *is* the captured graph. M32 (AMP) becomes a type-conversion pass rather than a dispatch rewrite.
- **It lifts the fusion ceiling** listed at the bottom of this document. `x.matmul(W).relu()` fuses under `-linalg-fuse-elementwise-ops` instead of materializing an intermediate.
- **It is the honest version of "learning how compilers work."** Progressive lowering across dialects is how every modern ML compiler is built; hand-rolling strided C loops teaches less and costs more.

The costs, stated plainly so they aren't discovered mid-milestone:

- **A ~1.2 GB build dependency** and an LLVM version bump (see the prerequisite section). Gated behind `-DZ_ENABLE_MLIR=ON` so M0–M16 never pay it.
- **TableGen enters the build.** The `z` dialect is defined in `ZDialect.td` and generates C++ via `mlir_tablegen`. This is a new build concept to learn.
- **A second IR in the compiler.** See the interop rule below — getting this boundary wrong is the main risk of this decision.

**The interop rule (the important part).** The compiler does **not** move to MLIR wholesale. Scalars, control flow, functions, classes, and every library from M0–M16 keep emitting LLVM IR directly through the existing `CodeGen.cpp`. MLIR is used *only* for tensor-valued expressions, as a side module:

1. `CodeGen` walks a function. On hitting a tensor expression it hands the sub-expression to `TensorEmitter`, which builds a `func.func` in a separate `mlir::ModuleOp` and returns the generated function's symbol name and signature.
2. At the original site, `CodeGen` emits an ordinary LLVM `call` to that symbol, passing memref descriptors (data pointer, offset, sizes, strides) unpacked from the `ZTensor` struct.
3. After the whole program is walked, `Pipeline.cpp` runs the lowering pipeline on the MLIR module, translates it to an `llvm::Module` via `mlir::translateModuleToLLVMIR`, and `llvm::Linker` merges it into the main module before the existing O2 pass run.

`ZTensor` stays exactly as specified in M17 — MLIR operates on the buffer it points at, not on the struct. This keeps the GC, the runtime, and the ABI unchanged, and means a broken MLIR pipeline degrades to "tensor ops don't compile," never "the language doesn't compile." A wholesale migration to MLIR-as-primary-IR is a plausible post-v1 project; it is explicitly not this plan.

All five decisions should be recorded in the repo (e.g. `docs/decisions/0001-memory.md`, `0002-errors.md`, `0003-stdlib-hosting.md`, `0004-generics.md`, `0005-tensor-lowering.md`) before M6 starts — otherwise they quietly diverge across milestones.

---

## Implementation Milestones

**Milestone order at a glance:**

| # | Title | Why here |
|---|-------|----------|
| M0  | "Hello 42" pipeline | Prove toolchain end-to-end |
| M1  | Variables & arithmetic | Smallest useful language |
| M2  | Functions & control flow (`if`, `else`, `while`, `for`, `switch`), `break`, `continue` | Multi-function programs with full structured control flow and loop exits |
| M3  | Full type system — all primitives including `string`, `null`, `dynamic`, ternary, `static_cast` | All scalar types; `string` has `+` and comparisons built-in; all named ops are free functions in M6; `null`; `break`/`continue` |
| M4  | Namespaces + `using` import — `namespace NAME { }`, `using NAME`, qualified `NAME.symbol` | One unified mechanism for user-defined and built-in libraries; no special compiler registry |
| M5  | Extended namespaces — nested `namespace A { namespace B { } }`, dotted `using A.B` | Sub-area imports without pulling the whole library into scope |
| M6  | `string` library — named operations as free functions (`length`, `slice`, `contains`, `format`, …) | All string ops are free functions, not methods; first `using`-gated library |
| M7  | `math` library | Simplest library — external libm linkage smoke test |
| M8  | `datetime` library — `date`, `time` constructor calls, accessors, arithmetic, formatting | Library backed by libc `<time.h>`; no class system needed |
| M9  | `io` library — txt, JSON (raw), Word text extraction, QMD | File I/O from day one; full typed Word/JSON API expands after structures/classes land |
| M10 | `data` library — CSV (raw Phase A), DataFrame + Excel (Phase B after M13) | Tabular data wrangling; CSV ships now; full DataFrame API unlocked after classes |
| M11 | `structures` library — `array`, `vector`, `list`, `stack`, `queue`, `heap`, `bstree`, `map`, `unordered_map` | Adds the C-runtime linkage pattern for generic containers |
| M12 | `algorithms` library — sorting, searching, tree traversals | First library that composes on top of another (`structures`); first self-hosted stdlib (`stdlib/algorithms.z`) |
| M13 | Classes (leaky) — fields, methods, ctors, `new` | Smallest useful OOP surface. Heap objects are `malloc`'d and leak; the point is to prove the class system end-to-end |
| M14 | **Garbage Collector + destructors** — mark-and-sweep, shadow-stack roots | Adds the collector and `~ClassName()` finalizers. Retrofits `structures` (M11) onto the GC. First milestone where programs don't leak |
| M15 | Inheritance, interfaces, virtual dispatch | Polymorphism |
| M16 | Function & method overloading | Enables int overloads for `math`, `regression` ergonomics |
| M17a | **MLIR foundation** — build integration, `z` dialect skeleton, one op end-to-end | Stands up the second IR path and proves the LLVM-IR ↔ MLIR handoff on a trivial op, before any tensor semantics are at stake |
| M17b | `tensor` — **core** (CPU, dynamic + static shape, no autograd, no GPU) | Smallest useful tensor: construction, indexing, broadcasting, matmul, linalg (inverse/solve). Lowers through `linalg`. The foundation regression needs |
| M18 | `regression` library | Self-hosted stdlib in `stdlib/regression.z` — built on M17b tensors. Exercises M13/M15 classes in real Z code. Depends on M13, M15, M16 |
| M19 | `tensor` — **autograd** (`.backward()`, `.grad()`, `with no_grad()`) | Tape-based reverse mode. Regression's iterative solvers can be rewritten on top of it post-landing |
| M20 | `tensor` — **NVIDIA CUDA backend** (`.to(CUDA)`, `linalg` → `gpu` → `nvvm` → PTX, cuBLAS/cuDNN for matmul/conv) | First GPU backend. Kernels are generated from the same `linalg` ops as CPU, not hand-written. Regression and `nn` inherit GPU acceleration automatically |
| M21 | `tensor` — **AMD ROCm/HIP backend** (`.to(ROCm)`, `linalg` → `gpu` → `rocdl` → HSACO, rocBLAS/MIOpen) | Second GPU vendor. Mostly a target swap in the existing `gpu`-dialect pipeline; tests vendor-portability of the lowering, not of a hand-written kernel library |
| M22 | `tensor` — **fusion & static-shape tuning** (pipeline configuration: fuse, tile, vectorize, unroll on known shapes) | Performance polish, optional. Pass-pipeline work, not new codegen |
| M23 | `nn` — **module system** (`Module`, `Parameter`, `Buffer`, `forward`, training/eval modes, parameter iteration) | The PyTorch `nn.Module` analogue — the foundation every layer and model is built on |
| M24 | `nn` — **basic layers & activations** (`Linear`, `ReLU`, `Sigmoid`, `Tanh`, `GELU`, `Softmax`, `Dropout`, `BatchNorm`, `LayerNorm`, `Sequential`) | Smallest set of layers needed to build a feedforward network |
| M25 | `nn.loss` — **loss functions** (`MSELoss`, `L1Loss`, `BCELoss`, `CrossEntropyLoss`, `NLLLoss`, `KLDivLoss`, `HuberLoss`, `CosineEmbeddingLoss`) | Differentiable loss API; combined with M23/M24 enables a complete training step |
| M26 | `optim` — **optimizers & schedulers** (`SGD`, `Momentum`, `Adam`, `AdamW`, `RMSprop`, `Adagrad`; `StepLR`, `ExponentialLR`, `CosineAnnealingLR`, `ReduceLROnPlateau`) | First milestone where a complete training loop is expressible end-to-end |
| M27 | `data.loader` — **Dataset, DataLoader, samplers, transforms** (extends M10 `data`) | Mini-batch iteration, shuffling, multi-worker prefetch (single-thread first; threading post-M34) |
| M28 | `nn` — **convolutional layers** (`Conv1d`, `Conv2d`, `Conv3d`, `ConvTranspose2d`, `MaxPool2d`, `AvgPool2d`, `AdaptiveAvgPool2d`) | Image-domain layers; uses cuDNN/MIOpen on GPU when available |
| M29 | `nn` — **recurrent layers** (`RNN`, `LSTM`, `GRU`, packed sequences, bidirectional, multi-layer) | Sequence-domain layers; the second non-trivial layer family after conv |
| M30 | `nn` — **attention & transformer** (`MultiheadAttention`, `TransformerEncoderLayer`, `TransformerDecoderLayer`, `TransformerEncoder`, positional encoding) | Modern architectures; closes the gap with current PyTorch usage |
| M31 | `nn` — **serialization & checkpointing** (`state_dict`, `load_state_dict`, `save`, `load`, parameter naming) | Checkpoints survive process restarts; supports transfer learning and resuming training |
| M32 | `amp` — **automatic mixed precision** (`autocast`, `GradScaler`, fp16/bf16 dispatch) | Cuts GPU memory and time roughly 2× with no API change for trained models |
| M33 | `jit` — **graph capture, scripting, ONNX-style export** | Decouples model definition from inference runtime; enables deployment outside the Z toolchain. MLIR bytecode is the capture format, so this is largely serialization plumbing |
| M34 | `distributed` — **data-parallel training** (`DistributedDataParallel`, `all_reduce`, NCCL/RCCL backends, multi-GPU, multi-node) | The final "real PyTorch" feature; combines M20/M21 backends with M23/M26 training loop |

Phases:
- **Phase 1 — Language core (M0–M3):** the minimal imperative language with a real type system, `null`, and loop control (`break`/`continue`). `string` is a primitive keyword — variables, literals, `null` assignment, `print`, `+` concatenation, and all six comparison operators work without any import. Named operations (`length`, `contains`, `slice`, etc.) are free functions added in M6.
- **Phase 2 — Libraries (M4–M12):** namespaces + import (M4), extended nested namespaces (M5), then `string` (M6), `math` (M7), `datetime` (M8), `io` (M9), `data` (M10, Phase A), `structures` (M11), `algorithms` (M12). These libraries don't need classes. `io` and `data` ship partial APIs now and expand once classes (M13+) and structures (M11+) land.
- **Phase 3 — OOP (M13–M16):** classes (M13), GC + destructors (M14), inheritance and virtual dispatch (M15), and overloading (M16). The M13/M14 split keeps the "OOP surface works" milestone independent from the "runtime owns lifetimes" milestone.
- **Phase 4 — Numerical stack (M17–M18):** the MLIR foundation (M17a), the smallest useful tensor on top of it (M17b), then regression (M18). This is where the compiler transitions from "compiles programs" to "hosts a real library ecosystem," and where it grows its second IR. M17a is a hard gate: nothing in M17b should be attempted until a trivial op round-trips from Z source through the `z` dialect to a linked LLVM function.
- **Phase 5 — Tensor upgrades (M19–M22):** autograd (M19), the NVIDIA CUDA backend (M20), the AMD ROCm/HIP backend (M21), and fusion/static-shape pipeline tuning (M22). Each can ship on its own timeline; none block regression or any earlier milestone. All four are pipeline work on the M17a foundation rather than four independent runtimes.
- **Phase 6 — NN training core (M23–M27):** the minimum stack to train a feedforward neural network end-to-end — the `nn.Module` system (M23), basic layers and activations (M24), losses (M25), optimizers and schedulers (M26), and data loading (M27). After M27, a user can write a full PyTorch-style training loop in Z.
- **Phase 7 — Advanced layer families (M28–M30):** convolutions (M28), recurrent layers (M29), and attention/transformer blocks (M30). Each is an independent layer family that drops into the M23 module system without changing earlier milestones.
- **Phase 8 — Production features (M31–M34):** checkpointing (M31), automatic mixed precision (M32), JIT/graph export (M33), and distributed training (M34). These are the features that turn a "can-train-models" library into a production-grade ML stack. All are optional and orderable independently of one another.

Dependency rationale:
- Namespaces + `using` land at M4 so every library from M6 onward is just a namespace — no special compiler registry for built-in libs, no different mechanism for user-defined libs. From M6, `using string` and `using mylib` work identically.
- `string` extended operations (M6) come first among libraries: the `string` keyword is already a primitive from M3, but the extended methods (`.slice`, `.split`, `.format`, etc.) are the first `using`-gated feature. Almost every library from M7 onward accepts or returns strings, so the extended API should exist before they do.
- `math` (M7) ships `double`-only versions of `abs`/`min`/`max` first; integer overloads are added in an addendum once overloading lands in M16.
- `datetime` (M8) sits before `structures` because it has no dependency on containers and exercises the libc-stateful-runtime pattern in isolation. Constructor-call syntax (`date(2026, 4, 22)`) avoids the lexer ambiguity of literal forms.
- `structures` (M11) holds every container including `array<T>` — uniform import story.
- `algorithms` (M12) sits between containers and OOP — uses `structures`, doesn't need classes, lets us validate cross-library composition before the OOP retrofit.
- OOP (M13/M14/M15) lands before overloading (M16) because method overloading is the main motivator for overloading.
- **Tensor is sliced, not monolithic.** The original plan had one giant tensor milestone covering core + autograd + GPU + static-shape. That's ~4 separate projects pretending to be one milestone. Splitting them (M17b, M19, M20, M21) makes each independently shippable and lets `regression` sit on just the core (M17b) instead of blocking on CUDA.
- **MLIR lands before tensor semantics, not with them (M17a → M17b).** The tensor-lowering decision above makes `tensor` the first library with a second IR behind it. Bundling "learn MLIR, wire up TableGen, get the LLVM-IR ↔ MLIR handoff right" into the same milestone as "implement broadcasting and matmul" guarantees that a pipeline bug and a semantics bug will be indistinguishable. M17a's deliverable is deliberately trivial — one elementwise op, no broadcasting, no dtype promotion — because its real content is build integration and the module-merge boundary.
- **The GPU milestones shrink under MLIR.** Under the old C-runtime model, M20 and M21 each meant hand-writing a kernel per op in CUDA and then again in HIP. Under `linalg` → `gpu` dialect, both are lowering targets for ops that already exist: M20 adds the `nvvm` path plus cuBLAS/cuDNN bindings for matmul and conv, M21 swaps in `rocdl` plus rocBLAS/MIOpen. The `ZDeviceOps` vtable from M20 survives, but it dispatches *library calls and compiled kernel launches*, not 40 hand-written implementations.
- **M22 changes character.** It was "write a third implementation of every op as inline LLVM loop nests." It becomes "configure and tune the pass pipeline for compile-time-known shapes." Same goal, an order of magnitude less code, and it is the milestone where the fusion ceiling actually lifts.
- `regression` (M18) sits on `tensor` (M17b) the same way sklearn sits on numpy — there is no parallel linalg stack, no `Matrix`/`Vector` duplicate of `tensor`. Iterative solvers initially use hand-written gradients; when M19 (autograd) lands, they can be rewritten to use `.backward()` without changing the public API. `regression` also depends on M13 (classes), M15 (inheritance), and M16 (overloading).
- **Phase 5 is optional.** M19/M20/M21/M22 can all be deferred, reordered, or split further without disturbing M0–M18. The two GPU backends (M20 NVIDIA CUDA, M21 AMD ROCm/HIP) are explicit "own project" escape hatches — each is large enough to be a separate effort, but they share a device-abstraction layer so the second one is significantly cheaper than the first.
- **GPU backends are split, not unified.** Earlier drafts had a single "GPU" milestone that conflated CUDA and ROCm. They are different runtimes (NVCC/HIP), different libraries (cuBLAS/rocBLAS, cuDNN/MIOpen), and different communication backends (NCCL/RCCL). Doing one first proves the device-abstraction layer; the second validates portability. The internal `Device` enum is `CPU | CUDA | ROCm` from M20 onward, even though only one backend exists at that point — this keeps the API stable across M21.
- **NN milestones build on tensor + classes, not on a parallel runtime.** M23 (`nn.Module`) is just a Z class with a virtual `forward()` method (M15) and an automatic parameter-registration mechanism on top of M19 autograd. There is no special "module runtime" — `nn` is self-hosted in `stdlib/nn.z` (and submodules) the same way `regression` is in `stdlib/regression.z`. This keeps the GPU dispatch identical for `nn` and for hand-written tensor code: a `Linear` layer on `CUDA` is just two GPU matmuls.
- **Training-loop ordering (M23 → M27).** The dependencies are: `Module` (M23) → layers (M24) → loss (M25) → optimizer (M26) → data loader (M27). After M26 a user can write a full training loop using hand-batched tensors; M27 just makes batching ergonomic. This is intentional — losses and optimizers are smaller and more standalone than data loading, which interacts with concurrency.
- **Advanced layer families (M28–M30) are parallel.** Conv (M28), RNN (M29), and attention (M30) all sit on M24's layer protocol but do not depend on each other. A team could ship them in any order, or stop after one. Each requires its own runtime piece (cuDNN/MIOpen for conv; cuDNN RNN routines for M29; flash-attention-style fused kernel for M30 — initially fall back to per-step matmul).
- **Production milestones (M31–M34) are independent.** Each can ship without the others. Order shown is rough difficulty, not strict dependency. M34 (distributed) does have a soft dependency on M32 (AMP) for realistic-scale training but works without it.
- **What is *not* in the plan:** dynamic graphs at the C++ level (the autograd tape is the dynamic graph), Python interop, `torch.fx` graph transforms, `torch.compile` (PyTorch 2.x JIT) — M33's graph capture is a smaller "scripting"-style export. Quantization, sparse tensors, custom CUDA kernels written in Z — all explicitly post-v1.

### Milestone 0: "Hello 42" — Prove the pipeline works

**Goal:** `fn main() -> int { print(42) return 0 }` compiles to a working `.exe`.

**Files to create:**
1. `CMakeLists.txt` — find & link LLVM
2. `src/Token.h` — minimal token set (FN, IDENT, INT_LIT, LPAREN, RPAREN, LBRACE, RBRACE, ARROW, INT, RETURN, EOF)
3. `src/Lexer.h/.cpp` — tokenize the subset
4. `src/AST.h` — Program, FnDecl, BlockStmt, ReturnStmt, ExprStmt, IntLitExpr, CallExpr
5. `src/Parser.h/.cpp` — parse the subset
6. `src/CodeGen.h/.cpp` — generate LLVM IR: main function, `printf("%d\n", 42)`, `ret i32 0`
7. `src/main.cpp` — glue: read file → lex → parse → codegen → emit .obj → shell to clang for linking

**No Sema needed yet.** Skip type checking entirely.

### Milestone 1: Variables and Arithmetic

**Add:** `let` declarations, integer variables, `+`, `-`, `*`, `/`, `%`, assignment, `print(expr)`.

- Extend Token.h: TK_Let, TK_Colon, TK_Eq, TK_Plus, TK_Minus, TK_Star, TK_Slash, TK_Percent
- New AST nodes: LetStmt, AssignStmt, BinaryExpr, IdentExpr
- Parser: expression parsing with operator precedence
- CodeGen: `alloca`/`store`/`load` for variables, `CreateAdd`/`CreateSub`/etc.
- Begin basic Sema: symbol table, undeclared variable detection

### Milestone 2: Functions and Control Flow

**Add:** multiple functions, parameters, `if (cond) {}` / `else`, `while (cond) {}`, `do { } along (cond)`, `for (init; cond; step) {}`, `switch (expr) { case ...: { ... } default: { ... } }`, comparison & logical operators, `break`, `continue`.

**Loop summary — three distinct iteration forms:**
| Form | Check | Guaranteed first iteration |
|---|---|---|
| `while (cond) { }` | Pre-test | No |
| `do { } along (cond)` | Post-test | **Yes** |
| `for (init; cond; step) { }` | Pre-test | No |

`do ... along` is Z's post-test loop. The body always executes at least once; the condition is evaluated after the body and the loop continues if it is true. The keyword `along` is chosen to be distinct from `while` — using `while` as the post-test keyword (`do { } while (cond)`) would reuse a pre-existing token and create parsing ambiguity after a `do` block.

```z
let i: int = 0
do {
    print(i)
    i = i + 1
} along (i < 5)
# prints 0 1 2 3 4 — body ran before first check
```

- Parser: require `(` after `if`/`while`/`for`/`switch`/`along` keywords — error if missing. `do` takes no condition; `along` is the keyword that closes the loop. `do { body } along (cond)` — both keywords are required.
- New AST:
  - `IfStmt`, `WhileStmt`, `UnaryExpr`
  - `DoAlongStmt { BlockStmtPtr body; ExprPtr cond; }` — body first, condition checked after
  - `ForStmt { StmtPtr init; ExprPtr cond; StmtPtr step; BlockStmtPtr body; }` — both `init` and `step` reuse the existing `LetStmt` / `AssignStmt` nodes
  - `SwitchStmt { ExprPtr scrutinee; vector<CaseArm> cases; BlockStmtPtr defaultArm; }`, `CaseArm { ExprPtr value; BlockStmtPtr body; }`
- New tokens: `Do`, `Along`, `Break`, `Continue`
- New AST nodes: `BreakStmt`, `ContinueStmt`
- CodeGen:
  - Conditional branching (`CreateCondBr`), function params, comparison ops
  - **`do ... along`:** lower to `body` block (run statements) → `cond` block (eval cond, `condBr body / exit`) → `exit` block. No pre-check — first iteration is unconditional.
  - **For loop:** lower to `init` block → `br header`; `header` block (eval `cond`, `condBr body / exit`); `body` block (statements, then `br step`); `step` block (run step, `br header`); `exit` block. Variables declared in `init` are scoped to the loop body.
  - **Switch:** lower to `builder.CreateSwitch(scrutinee, defaultBB, numCases)` then add cases with `sw->addCase(constInt, caseBB)`. Each case BB ends with a `br merge` once its block runs (no implicit fallthrough). LLVM's backend converts to a jump table when dense, a binary tree of compares when sparse — automatic, no extra work.
  - **`break`:** emit `br exitBB` where `exitBB` is the exit block of the nearest enclosing loop or `switch`. CodeGen maintains a loop-exit-block stack; `break` pops the top and jumps there.
  - **`continue`:** emit `br stepBB` (for `for` loops) or `br condBB` (for `while` / `do...along`) — the "next iteration" entry point. CodeGen maintains a parallel loop-continue-block stack. A `continue` inside a `switch` nested inside a loop targets the loop's continue block, not the switch.
- Sema:
  - Function signature table, argument type checking, scope push/pop
  - **For loop:** push a new scope for the `init` declaration so it shadows any outer name only inside the loop. Pop on exit.
  - **Switch:** scrutinee must be an integer or character type; reject `switch` on `dynamic` / `double` (no equality semantics that match `case`). Reject duplicate case values with the location of the prior case. `default` is optional but at most one per switch.
  - **`break`:** Sema error if not inside a loop or `switch`. Tracks enclosing context with a stack of `{loop, switch}` tags.
  - **`continue`:** Sema error if not inside a loop (`while`, `for`, `do...along`). Sema error if the immediately enclosing statement is a `switch` that is not itself inside a loop.

### Milestone 3: Full Type System

**Add:** the full primitive set: `int`, `int32`, `int64`, `int128`, `float16`, `float`, `double`, `bool`, `character`, `string`, `dynamic`; the `null` value; the ternary operator `cond ? a : b`; and `static_cast<T>(expr)` for explicit narrowing.

**Status as of the current tree (M2 is landed; M3 is partially scaffolded).** M0–M2 compile and run, minus the items called out below. Several M3 pieces exist as *tokens or AST nodes with no path through the pipeline* — the enum entry is there, nothing consumes it. Work items, in dependency order:

*Carried over from M2 — finish these first, they are cheap and the M3 table row claims them:*
1. **`break` / `continue` are not implemented.** `TokenType::Break` and `Continue` exist in `Token.h`, but `scanIdentifierOrKeyword` never produces them, there are no `BreakStmt` / `ContinueStmt` AST nodes, no Sema context stack, and no CodeGen block stacks. Implement per the M2 spec above.
2. **CodeGen has no scope stack.** `Sema` correctly pushes and pops scopes, but `CodeGen::symbols_` is one flat `map<string, AllocaInst*>` per function. A shadowed `let` in a nested block overwrites the outer binding and never restores it. Mirror Sema's scope discipline before more types make the failure subtler.
3. **No "all paths return" check.** `genFnDecl` emits `CreateUnreachable` when a function body falls off the end, turning a missing `return` into undefined behavior instead of a diagnostic. Add the check in Sema.
4. **Switch validation is missing.** Sema does not reject duplicate `case` values, does not enforce at most one `default`, and does not require case values to be constants — CodeGen throws a raw error late instead. The plan requires all three in Sema.

*The type-dispatch refactor — do this before the string and dynamic work, everything else depends on it:*

5. **CodeGen dispatches on LLVM types instead of `resolvedType`.** This is already a live bug, not a future one: `character` and `int32` are both `i32`, so `let ch: character = 'Z'  print(ch)` prints `90` rather than `Z`. `string` and `dynamic` will both be pointers, so neither can be distinguished either. See the "CodeGen must dispatch on Z types" note in Key Implementation Details. Fix the `print` dispatch, the binary-operator dispatch, and `coerce` to key off `Expr::resolvedType`.
6. **`character` is spelled `char`.** The language spec, grammar, and every example in this document use `character`; the lexer accepts `char`. Pick `character` per the spec, and update `Test/codegen/m3_types.z`.

*New M3 surface:*

7. **`static_cast<T>(expr)` does not parse.** This is the closest thing to free work in the milestone: `TokenType::StaticCast` is lexed, `CastExpr` exists in the AST, `Sema::resolveExpr` handles it, and `CodeGen::genExpr` emits it. Only the parser production is missing — `static_cast` `<` type `>` `(` expr `)`, reusing the existing `Less` / `Greater` tokens.
8. **`null`.** `TokenType::Null` / `Nullptr` exist but the lexer never emits them. Needs the keyword, a `NullLitExpr`, a `TypeRef::Null`, Sema's assignability and comparison rules, and `ConstantPointerNull` in CodeGen.
9. **Ternary `cond ? a : b`.** Nothing exists — needs a `Question` token, `TernaryExpr`, `parseTernaryExpr` between `parseExpr` and `parseOrExpr`, Sema's common-type resolution, and `CreateSelect`.
10. **`string` as a real type.** Currently a literal lowers to a NUL-terminated C string global, `+` on two strings is a Sema error (`isNumeric(String)` is false), and comparisons are rejected. Needs the `ZString` representation, the runtime functions, literal emission as global `ZString` constants, and the `+` / six-comparison operator overloads. This is the largest single item in M3.
11. **`dynamic`.** Nothing exists — no keyword, no `TypeRef`, no runtime. Needs `zdynamic.c`, boxing on assignment, `static_cast` unboxing, and tag-dispatched `print`.
12. **`dynamic_cast<T>(expr)`.** Nothing exists. Smaller than `static_cast` once `dynamic` is in place, since only the `dynamic` source case matters until classes land in M13.

*Infrastructure the milestone forces:*

13. **There is no runtime library and no way to link one.** `Runtime/Headers/` and `Runtime/Main/` are empty directories, and the driver's link step is `clang <obj> -o <exe>` with nothing else. M3 is the first milestone that needs C runtime code, so `zruntime` and the driver's link line both have to exist before items 10 and 11 can be tested. See the CMakeLists notes in Key Implementation Details.
14. **`Test/` is empty.** No test runner, no expected-output files, nothing to catch regressions. M3 roughly triples the type-interaction surface — the point at which "run the example and eyeball it" stops working. Stand up the `Test/{lexer,parser,sema,codegen}` layout and the runner from the Testing Strategy section as part of this milestone.
15. **`--dump-ast` is stale.** It handles only `Return` / `Expr` / `Let` / `Assign` statements and four expression kinds; every M2 control-flow node and every M3 expression prints as `UnknownStmt` / `UnknownExpr`, which makes it useless for exactly the tests item 14 introduces.
16. **`Include/Types.h` is empty and `TypeRef` lives in `AST.h`.** A flat enum is already strained by `float32`/`float` synonyms, and it cannot represent `vector<int>` (M11) or `tensor<float, 2, 2>` (M17b) at all. M3 does not require the full `GenericType` node from the generics decision, but it is the right moment to move type representation into `Types.h` behind a small struct so the later change is additive rather than a rewrite of every `switch` in Sema and CodeGen.

`string` is a **keyword primitive** — no `using` required, exactly like C#'s `string`. String literals (`"hello"`), the `+` concatenation operator, comparison operators, and a small set of built-in methods are part of the language core. Extended operations (`.slice`, `.contains`, `.to_upper`, `.format`, etc.) live in the `string` standard library added in M6.

- Lexer: float/double literals (e.g. `3.14f` for `float`/`float32`, `3.14` for `double`). **No `float16` literal syntax** — `float16` is a storage/compute type, not a value you write by hand. Use an explicit cast: `static_cast<float16>(3.14)`. Add `dynamic`, `null`, and `string` as keyword tokens; `STRING_LIT` (`"..."`) always tokenized regardless of imports.
- LLVM type mapping:
  - `int` → `i64` (on 64-bit platforms)
  - `int32` → `i32`, `int64` → `i64`, `int128` → `i128`
  - `float16` → `half` (16-bit IEEE 754 — LLVM's `HalfTy`)
  - `float` → `float` (32-bit), `double` → `double` (64-bit)
  - `bool` → `i1`, `character` → `i32` (Unicode code point)
  - `string` → `ZString*` (pointer to heap struct — defined below)
  - `null` → `i8*` null pointer (typed `nullptr` at the LLVM level; Sema tracks which pointer types it is assignable to)
  - `dynamic` → pointer to `ZDynamic` (defined below)
- Sema: numeric type promotion rules — implicit widening only (`float16` → `float` → `double`, `int32` → `int64` → `int128`); no implicit narrowing. Cross-kind promotion (int to float): `int` → `double` implicitly, smaller ints promote first. Forbid narrowing without an explicit cast. `dynamic` is always assignable from any primitive type (boxing); extracting from `dynamic` requires `static_cast<T>`. `null` is assignable to any pointer or reference type; comparing any pointer-typed variable to `null` with `==` / `!=` is always valid.
**`string` — primitive reference type:**

The `string` keyword and string literals (`"hello"`) are always recognized — no import needed. M3 provides the *type*, its runtime shape, the two built-in operators (`+` and comparisons), and compiler intrinsics for `print` and `null`. All named operations (length, search, slice, etc.) are free functions defined in the `string` library (M6).

What M3 provides for `string`:
- Variables and parameters: `let s: string = "hello"` compiles without any import.
- `null` assignability: `let s: string = null` is valid. `s == null` and `s != null` lower to an LLVM `icmp eq ptr, null` — pointer identity, not a string operation.
- `print(string)`: CodeGen dispatches `%s` via `z_string_cstr` (internal helper, not a library function).
- Boxing into `dynamic`: `DYN_STRING 6` tag is established here. Boxing stores the `ZString*` cast to `int64_t`; `z_dynamic_print` gains a `%s` branch; `static_cast<string>(dyn)` validates the tag.
- **`s1 + s2` — concatenation:** Sema overloads `+` for `string` operands and lowers to `z_string_concat(lhs, rhs)`, which allocates a new `ZString` of combined length. `+` between `string` and any non-`string` type is a Sema error.
- **`==`, `!=`, `<`, `<=`, `>`, `>=` — lexicographic comparison:** All six lower to `z_string_cmp(a, b)` (returns -1/0/1 byte-by-byte) then apply the relational check. These are built-in operators, not library functions.

Runtime representation (`runtime/zstring.h`):
```c
struct ZString {
    ZGCHeader header;    // typeinfo + mark_flags — dormant until M14 (GC)
    int64_t   length;    // byte count, not code-point count
    uint8_t   bytes[];   // UTF-8 payload, not null-terminated (length is authoritative)
};
```
String literals are allocated once at program load into an immortal region (flagged "always reachable" in their GC header once M14 exists). The struct shape is fixed here and never changes, so no M3–M13 code needs rewriting when the collector arrives.

*Runtime files added in M3:* `runtime/zstring.h`, `runtime/zstring.c` — `z_string_alloc`, `z_string_cstr`, `z_string_concat`, `z_string_cmp`. All named operation functions (`z_string_length`, `z_string_get`, `z_string_slice`, etc.) are added in M6.

- **Safe cast:** `dynamic_cast<T>(expr)` — safe extraction from a `dynamic` value; returns `0` (for numeric targets) or `null` (for string/class targets) if the runtime tag doesn't match, instead of aborting. For class downcasts (M17+): `dynamic_cast<Dog>(animalPtr)` returns `null` if the object is not a `Dog`. New AST node: `DynCastExpr { TypeRef targetType; ExprPtr operand; }`. New keyword: `dynamic_cast` (lexed as one token). CodeGen calls `z_dynamic_safe_unbox(tag, ptr)` for dynamic values, or emits `typeinfo` comparison + conditional null for class pointers (M17).
- **Explicit cast:** `static_cast<T>(expr)` — the only way to narrow or convert across kinds. Aborts on tag mismatch when unboxing `dynamic`. Examples: `static_cast<int>(3.14)` truncates double to int, `static_cast<float16>(x)` demotes float to half, `static_cast<int32>(n)` narrows int64, `static_cast<int>(dyn)` unboxes a dynamic. Sema checks that the cast makes sense (no casting `string` to `int`); CodeGen emits the appropriate LLVM cast instruction (`trunc`, `fptrunc`, `fptosi`, `sitofp`, `zext`, etc.) or calls `z_dynamic_unbox`. New AST node: `CastExpr { TypeRef targetType; ExprPtr operand; }`.
- CodeGen: format string selection in `print` — `%d` int32, `%lld` int64, `%f` float/float16 (promote to double for printf), `%lf` double, `%c` char, `%s` string (via internal `z_string_cstr` — not a library function, always available). For `dynamic`, print dispatches on the runtime tag via `z_dynamic_print`.
- **`float16` arithmetic note:** LLVM emits `half` typed IR. On x86 without AVX-512 FP16 support, LLVM will lower `half` ops via software emulation or promote to `float` internally. On modern GPUs (M19) `half` maps directly to hardware FP16 instructions.

**Ternary operator `cond ? a : b`:**
```z
let max_val: int   = a > b ? a : b
let abs_val: float = x >= 0.0f ? x : -x
return n <= 1 ? 1 : n * factorial(n - 1)
```
- New token: `Question` (`?`). The colon `:` already exists.
- New AST node: `TernaryExpr { ExprPtr cond; ExprPtr thenExpr; ExprPtr elseExpr; }`.
- Precedence: between `||` (lower) and assignment (higher). Parser: add `parseTernaryExpr` between `parseExpr` and `parseOrExpr`.
- Sema: `cond` must be numeric; `thenExpr` and `elseExpr` must be the same type or promotable to a common type (same rules as arithmetic). Resolves to the promoted type.
- CodeGen: when both branches are the same LLVM type, use `CreateSelect(condBool, thenVal, elseVal)` — branch-free single instruction. When types differ after Sema coercion, coerce both to the common type first, then `CreateSelect`. `CreateSelect` requires both operands to already be the same type.

**`dynamic` type — runtime-typed value:**
```z
let x: dynamic = 42
let y: dynamic = 3.14
let z: dynamic = "hello"
print(x)   # 42       — dispatches to %lld at runtime
print(y)   # 3.140000 — dispatches to %lf at runtime
print(z)   # hello    — dispatches to %s at runtime
x = true   # re-assign to a different type — OK
let n: int = static_cast<int>(x)         # unbox numeric
let s: string = static_cast<string>(z)   # unbox string
```

Runtime representation (`runtime/zdynamic.h`):
```c
#define DYN_INT    1
#define DYN_FLOAT  2
#define DYN_DOUBLE 3
#define DYN_BOOL   4
#define DYN_CHAR   5
#define DYN_STRING 6   // ZString* cast to int64_t in data field

typedef struct ZDynamic {
    ZGCHeader gc_header;   // dormant until M14 GC
    int32_t   tag;         // DYN_* constant
    int64_t   data;        // value inline for primitives; ZString* cast for string
} ZDynamic;
```
- **Boxing (assignment to dynamic):** CodeGen calls `z_dynamic_box(tag, data)` — allocates a `ZDynamic`, sets tag and data.
- **Unboxing (static_cast from dynamic):** CodeGen calls `z_dynamic_unbox(tag, ptr)` — checks tag matches expected type (mismatch → `abort()` with message), returns the stored value. No implicit unboxing — always requires `static_cast<T>`.
- **`print(dynamic)`:** CodeGen detects the argument is `dynamic*` and calls `z_dynamic_print(ptr)` which switches on `tag` and dispatches to the right `printf` format.
- **Arithmetic on dynamic:** not allowed without explicit unbox. `x + y` where either is `dynamic` is a Sema error. Write `static_cast<int>(x) + static_cast<int>(y)`.
- **Sema rules:** `dynamic` is always a valid target for assignment from any primitive. `canWiden(anyPrimitive, Dynamic) = true`. Comparisons, arithmetic, and function parameters of concrete type require explicit cast from `dynamic`.
- **Runtime file:** `runtime/zdynamic.c` — `z_dynamic_box`, `z_dynamic_unbox`, `z_dynamic_print`. Small file, no dependencies beyond the tag constants.

**`date` and `time` are not core primitives** — they live in the `datetime` library (M7) with constructor-call syntax (`date(2026, 4, 22)`, `time(14, 30, 0)`). Keeping them out of the lexer avoids the look-ahead disambiguation that literal forms like `2026-04-22` (date) and `14:30:00` (time) would require against integer subtraction / member access.

### Milestone 4: Namespaces + `using` Import Syntax

**Add:** the `namespace NAME { ... }` declaration and the `using NAME` import mechanism. This is the single mechanism for both user-defined libraries and the built-in stdlib — after M4, there is no special "compiler registry" for built-in libraries; they are simply namespaces defined in stdlib source files.

**Why namespaces instead of a compiler registry:** a registry hard-codes which names are valid and forces every new library to modify the compiler. Namespaces let the user define `namespace mylib { ... }` in their own code and import it with `using mylib` — identical syntax to the stdlib. The built-in libraries become stdlib `.z` files that define namespaces, pre-parsed by the compiler on startup.

**Namespace declaration:**
```z
# Defined in any .z file (including user files)
namespace mymath {
    fn square(x: int) -> int { return x * x }
    fn cube(x: int)   -> int { return x * x * x }
}

namespace mymath {           # Same namespace, different block — symbols merge
    fn fourth(x: int) -> int { return x * x * x * x }
}
```

**Import and qualified access:**
```z
using mymath                 # brings square, cube, fourth into scope

fn main() -> int {
    print(square(5))         # 25  — unqualified via `using`
    print(mymath.cube(3))    # 27  — qualified, no `using` needed
    return 0
}
```

**Rules:**
- `using NAME` must appear at the top of the file, before any `fn`/`class`/`namespace` declaration. Duplicate `using NAME` is a compile error. There is no exception — the import section is always the first thing in a file.
- Multiple `namespace NAME { }` blocks in the same or different files contribute to the same namespace — symbols are merged.
- Qualified access `NAME.symbol(args)` works without `using NAME` — namespaces are always visible for qualified lookup.
- Two namespaces can define functions with the same name (overloads across namespaces). Importing both via `using` works if the signatures don't conflict; if they do, qualified access is required.
- Nested namespaces (`namespace A { namespace B { } }`) are not supported in M4 — they are introduced in M5. A nested `namespace` block inside another is simply not parsed at this stage (the parser will stop at the inner `namespace` keyword and emit a "unexpected token" error).

**Files and implementation:**
- Extend Token.h: `TK_Using`, `TK_Namespace`
- Lexer: emit `TK_Using` for `using`, `TK_Namespace` for `namespace`
- AST:
  - `UsingDecl { string namespaceName }` — extend `Program` with `vector<UsingDecl>`
  - `NamespaceDecl : Decl { string name; vector<DeclPtr> decls }` — top-level declaration just like `FnDecl`
- Parser:
  - `parseUsingDecl` — `using IDENT newline`; must appear before any other top-level declaration
  - `parseNamespaceDecl` — `namespace IDENT { { top_decl } }` (functions, classes, interfaces; no nested namespaces)
- Sema — three-pass change:
  - **Pass 0 (new):** walk all top-level decls; for each `NamespaceDecl`, push all its child decls into `namespace_table_[name]`. Multiple blocks with the same name merge.
  - **Pass 1 (existing):** register function signatures — now also registers functions from imported namespaces.
  - **Pass 2 (existing):** check function bodies.
  - `using NAME` → inject all symbols from `namespace_table_[NAME]` into the current file scope. Unknown namespace → `"unknown namespace 'NAME'"`.
  - Qualified `NAME.symbol` → look up `namespace_table_[NAME]`, find `symbol` — no `using` needed.
- Mangling: namespace-scoped functions mangle as `NS__name__<paramCodes>` — e.g., `mymath.square(int)` → `mymath__square__i64`. This avoids conflicts between `fn square` in `namespace mymath` and `fn square` in `namespace mygeom`.

**Built-in libraries become namespaces:**
From M6 onward, every stdlib library is defined as a namespace in a `stdlib/*.z` file:
```z
# stdlib/math.z  (pre-parsed by the compiler at startup)
namespace math {
    fn sqrt(x: double) -> double { ... }   # backed by libm
    fn PI() -> double { return 3.14159265358979 }
    ...
}
```
`using math` is no longer a special compiler flag — it's an ordinary `using` that imports the `math` namespace. The compiler pre-parses stdlib files into the namespace table on startup, making built-in symbols available exactly like user-defined ones. No special registry, no hardcoded names.

**Test programs:**
- `tests/namespaces/user_defined.z` — define `namespace mymath`, call `square` via `using` and `mymath.cube` via qualified access
- `tests/namespaces/merge.z` — two `namespace foo` blocks; verify symbols from both are accessible after `using foo`
- `tests/namespaces/qualified_no_using.z` — qualified access `ns.fn()` without `using ns` — must compile
- `tests/namespaces/conflict.z` — two namespaces both export `sort`; `using` both → Sema error; qualified access resolves
- `tests/namespaces/unknown.z` — `using foo` with no `namespace foo` defined → `"unknown namespace 'foo'"`
- `tests/namespaces/duplicate_using.z` — two `using math` lines → error
- `tests/namespaces/misplaced.z` — `using math` after a `fn` decl → error (using must be at the top)

### Milestone 5: Nested Namespaces + Dotted Imports

**Add:** nested namespaces and dotted `using` imports — building directly on the flat namespace mechanism from M4. This milestone lifts the M4 restriction: `namespace A { namespace B { } }` is now valid. After M5, library authors can organize by sub-area and users can import just the sub-area they need without pulling the entire library into scope.

**Nested namespace declaration:**
```z
namespace math {
    fn sqrt(x: double) -> double { ... }

    namespace integral {
        fn trapezoid(f: ..., a: double, b: double, n: int) -> double { ... }
        fn simpson(f: ..., a: double, b: double, n: int)   -> double { ... }
    }

    namespace stats {
        fn mean(xs: array<double>) -> double { ... }
        fn variance(xs: array<double>) -> double { ... }
    }
}
```

**Import and qualified access:**
```z
using math              # imports sqrt, plus all sub-namespace symbols flattened (trapezoid, simpson, mean, variance)
using math.integral     # imports only trapezoid and simpson
using math.stats        # imports only mean and variance

math.integral.trapezoid(f, 0.0, 1.0, 100)   # qualified — no using needed
math.stats.mean(xs)                          # qualified
```

**Rules (extending M4, not replacing it):**
- `namespace A { namespace B { } }` — nesting allowed to any depth. Each level adds a dot to the fully qualified name: `A`, `A.B`, `A.B.C`.
- `using A` — imports all symbols declared directly in `A` AND all symbols from every sub-namespace of `A`, recursively flattened. This is the C# behavior: `using System` pulls in everything under `System.*`.
- `using A.B` — imports only the symbols declared directly inside `A.B`. Does not import `A`'s own symbols. Does not recurse into `A.B.C` children. This is C#'s `using System.Collections` behavior — targeted sub-area import.
- `A.B.symbol(args)` — fully qualified access without any `using`. The compiler resolves by looking up `A.B` in the namespace table.
- Duplicate symbol across different `using` imports is a Sema error; use fully qualified access to disambiguate.
- `using A` must still appear at the top of the file before any declarations — the M4 placement rule is unchanged.

**Implementation changes on top of M4:**
- **AST:** `NamespaceDecl` already has `vector<DeclPtr> decls` — a child `NamespaceDecl` is just another element. No new AST node needed.
- **Sema pass 0 (namespace collection):** recurse into nested `NamespaceDecl`s, registering entries with dotted keys: `namespace_table_["math"]`, `namespace_table_["math.integral"]`, `namespace_table_["math.stats"]`. Parent namespace entry also stores child-namespace names for flattened `using A` expansion.
- **`using A`:** inject all symbols from `namespace_table_["A"]` plus all symbols from every `namespace_table_["A.*"]` child.
- **`using A.B`:** inject only `namespace_table_["A.B"]` — no recursion up or down.
- **Qualified access `A.B.fn(args)`:** Parser recognizes `Identifier.Identifier.Identifier(args)` as a `NamespaceAccessExpr { path: ["A","B"], name: "fn", args }`. Sema looks up `namespace_table_["A.B"]`, finds `fn`.
- **Mangling:** `A.B.fn(int)` → `A__B__fn__i64`.

**M4 backward compatibility:** flat `namespace foo { }` and `using foo` still work exactly as before. M5 is purely additive — every M4 program compiles unchanged under M5.

**Test programs:**
- `tests/namespaces/nested_declare.z` — `namespace math { namespace integral { fn trap... } }`, call `math.integral.trapezoid` qualified
- `tests/namespaces/dotted_using.z` — `using math.integral` imports only trapezoid/simpson, not math's own functions
- `tests/namespaces/flat_using_includes_children.z` — `using math` imports sqrt AND trapezoid AND mean
- `tests/namespaces/deep_qualified.z` — three levels deep `a.b.c.fn()` without any `using`
- `tests/namespaces/dotted_conflict.z` — `using math.stats` and `using mylib.stats` both export `mean` → Sema error; qualified resolves

### Milestone 6: `string` Library

**Add:** the `string` standard library, imported via `using string`. From M3 the `string` type, string literals, `+` concatenation, and the six comparison operators are already built-in. This milestone adds all named operations as **free functions** — they take the string as their first argument and are brought into scope by `using string`. There are no method calls on a string value; every named operation is a plain function call.

**Design principle:** Z's `string` library follows a procedural style. `length(s)` not `s.length()`. This is consistent with how the rest of the standard library works — `using math` gives you `sqrt(x)`, not `x.sqrt()`. All functions below are available unqualified after `using string`, or qualified as `string.length(s)` without the import.

**Full API (all require `using string`):**

*Basic access:*
- `length(s: string) -> int` — byte count of the string.
- `is_empty(s: string) -> bool` — `true` if length is 0.
- `get(s: string, i: int) -> character` — character at byte index `i`; `abort()` if out of range.

*Searching:*
- `contains(s: string, sub: string) -> bool` — `true` if `sub` appears anywhere in `s`.
- `starts_with(s: string, prefix: string) -> bool`
- `ends_with(s: string, suffix: string) -> bool`
- `index_of(s: string, sub: string) -> int` — byte index of first occurrence, or `-1` if not found.
- `last_index_of(s: string, sub: string) -> int` — byte index of last occurrence, or `-1`.
- `count(s: string, sub: string) -> int` — number of non-overlapping occurrences.

*Slicing and splitting:*
- `slice(s: string, start: int, end: int) -> string` — bytes `[start, end)`. `abort()` if range invalid.
- `split(s: string, sep: string) -> array<string>` — splits on every occurrence of `sep`. Requires `using structures` for the `array<string>` return type; Sema errors if `structures` is not imported.

*Case and whitespace:*
- `to_upper(s: string) -> string` — ASCII uppercase; non-ASCII bytes copied unchanged.
- `to_lower(s: string) -> string` — ASCII lowercase.
- `trim(s: string) -> string` — removes leading and trailing ASCII whitespace.
- `trim_start(s: string) -> string`, `trim_end(s: string) -> string`

*Transformation:*
- `replace(s: string, old: string, new_str: string) -> string` — replaces every occurrence of `old` with `new_str`.
- `pad_left(s: string, width: int, ch: character) -> string` — left-pads to `width` with `ch`.
- `pad_right(s: string, width: int, ch: character) -> string`
- `repeat(s: string, n: int) -> string` — concatenates `n` copies; `abort()` if `n < 0`.

*Construction and conversion:*
- `string(ch: character) -> string` — single-character string from a code point.
- `string(n: int) -> string` — decimal string representation of an integer.
- `string(b: bool) -> string` — `"true"` or `"false"`.
- `string(d: double) -> string` — decimal representation (6 significant digits).

*Static (namespace-qualified only):*
- `string.join(sep: string, parts: array<string>) -> string` — joins with separator. Requires `using structures`.
- `string.format(fmt: string, ...) -> string` — printf-style format string. Specifiers: `%d` (int), `%f` (float/double), `%s` (string), `%c` (character), `%b` (bool). `abort()` on type mismatch. Specifier count validated at compile time when `fmt` is a literal.

**Example:**
```z
using string
using structures   # needed for split — returns array<string>

fn main() -> int {
    let msg: string = "Hello, World!"

    # Built-in operators — no import needed
    print(msg + " How are you?")         # Hello, World! How are you?
    print(msg == "Hello, World!")        # true
    print(msg < "Z")                     # true

    # Free functions from using string
    print(length(msg))                   # 13
    print(is_empty(msg))                 # false
    print(get(msg, 0))                   # H
    print(contains(msg, "World"))        # true
    print(starts_with(msg, "Hello"))     # true
    print(index_of(msg, "World"))        # 7
    print(to_upper(msg))                 # HELLO, WORLD!
    print(slice(msg, 7, 12))             # World
    print(replace(msg, "World", "Z"))    # Hello, Z!
    print(trim("  hi  "))               # hi

    let parts: array<string> = split(msg, ", ")
    print(get(parts, 0))                 # Hello  — note: array.get, not string.get
    print(get(parts, 1))                 # World!

    let greeting: string = string(42) + " items"
    print(greeting)                      # 42 items

    let fmt: string = string.format("Value: %d, Pi: %f", 42, 3.14)
    print(fmt)                           # Value: 42, Pi: 3.140000

    return 0
}
```

**Implementation:**
- Self-hosted in `stdlib/string.z` as a `namespace string { ... }` block. Each function lowers to a corresponding C runtime call. Runtime functions added in M6 (on top of M3's `z_string_alloc`, `z_string_cstr`, `z_string_concat`, `z_string_cmp`): `z_string_length`, `z_string_get`, `z_string_slice`, `z_string_find`, `z_string_split`, `z_string_replace`, `z_string_format`, and the remaining helpers.
- `string.format` calls `vsnprintf` internally via a C helper; format string validated against argument types at runtime, and at compile time when the format is a literal.
- `split` and `string.join` are gated behind `using structures` — Sema checks both imports before allowing the call.

**Sema rules:**
- All functions in this milestone require `using string`. Calling `length(s)`, `contains(s, sub)`, etc. without the import is an "undeclared function" Sema error. Qualified calls (`string.length(s)`) resolve without `using string` via the namespace mechanism (M4).
- M3 built-ins (`+`, `==`/`!=`/`<`/`<=`/`>`/`>=`, `print`, `null` checks) are **never** gated behind `using string`.
- `string` cannot be used in `switch` case arms — no constant-expression equality semantics.
- Sema must not confuse `get(s, i)` (string get, returns `character`) with `get(arr, i)` (array get, returns element) — overload resolution on the first argument type disambiguates.

**Test programs:**
- `tests/string/no_import.z` — declare, assign literal, assign `null`, `s == null`, `print(s)`, `s + s`, `s == s` — all compile without `using string`; `length(s)` without import → Sema error
- `tests/string/basic.z` — `length`, `is_empty`, `get`
- `tests/string/search.z` — `contains`, `starts_with`, `ends_with`, `index_of`, `last_index_of`, `count`
- `tests/string/slice_split.z` — `slice`, `split` (requires `using structures`)
- `tests/string/transform.z` — `to_upper`, `to_lower`, `trim`, `trim_start`, `trim_end`, `replace`, `pad_left`, `pad_right`, `repeat`
- `tests/string/construct.z` — `string(int)`, `string(bool)`, `string(double)`, `string(character)`, `string.join`
- `tests/string/format.z` — `string.format` with all specifier types, literal format validation

---

### Milestone 7: `math` Library

**Add:** the `math` standard library — the first real library, imported via `using math`. Chosen as the first library because it's backed entirely by libm calls and requires no custom runtime code, giving a clean smoke test of the library registry and external linkage.

**API v1 (free functions, all `double`-precision):**
- `sqrt(x)`, `pow(x, y)`, `exp(x)`, `log(x)` — natural log
- `sin(x)`, `cos(x)`, `tan(x)`
- `abs(x: double)`, `floor(x)`, `ceil(x)`
- `min(a: double, b: double)`, `max(a: double, b: double)`
- Constants: `PI: double`, `E: double`

**Implementation:**
- Add `math` to the compiler's library registry from M4. When `using math` is present, Sema injects these symbols into the file's scope.
- Backed by libm — CodeGen declares `sqrt`, `pow`, `sin`, `cos`, `tan`, `exp`, `log`, `floor`, `ceil`, `fabs`, `fmin`, `fmax` as external functions in the LLVM module on first use and emits direct calls.
- `PI` and `E` are emitted as LLVM global `double` constants (or folded inline on reference — folding is simpler).
- Sema: reject references to `sqrt`, `PI`, etc. when `using math` is absent.

**Deferred to M15 addendum (once overloading exists):** integer overloads for `abs`/`min`/`max` (`abs(int)`, `min(int, int)`, etc.). In M6 these names exist only in their `double` form — a call like `max(1, 2)` implicitly widens the integer args to `double`, or the user casts explicitly.

**Test program:** `using math` + circle-area calculation with `PI * pow(r, 2.0)` + triangle hypotenuse via `sqrt(a*a + b*b)`.

### Milestone 8: `datetime` Library

**Add:** the `datetime` standard library — `date` and `time` types with constructor calls, accessors, arithmetic, and formatting — imported via `using datetime`. Sits at M7 because it depends only on `using` (M4) and the type system (M3); it does not need classes, the GC, or overloading.

**Why a library and not core language types:** literal forms like `2026-04-22` (date) and `14:30:00` (time) would force the lexer to look ahead and disambiguate from integer subtraction (`2026 - 04 - 22`) and member access. That context-sensitivity has no payoff for a learning compiler. Constructor-call syntax — `date(2026, 4, 22)`, `time(14, 30, 0)` — is unambiguous, lexer-friendly, and reads no worse for the few times it's actually written.

**API:**

*Construction:*
- `date(year: int, month: int, day: int) -> date` — validates ranges (1–9999 year, 1–12 month, 1–31 day with month-aware day check); `abort()` on invalid input per the error-handling decision.
- `time(hour: int, minute: int, second: int) -> time` — validates 0–23 / 0–59 / 0–60 (60 for leap second).
- `today() -> date` — current local date from `time(NULL)` + `localtime`.
- `now() -> time` — current local time-of-day.

*Accessors:*
- `date.year() -> int`, `.month() -> int`, `.day() -> int`, `.day_of_week() -> int` (0 = Sunday)
- `time.hour() -> int`, `.minute() -> int`, `.second() -> int`

*Arithmetic:*
- `date.add_days(n: int) -> date` — returns a new date; negative n subtracts
- `date.diff(other: date) -> int` — signed day difference (`self - other`)
- `time.add_seconds(n: int) -> time` — wraps modulo 24 hours

*Comparison:* `==`, `!=`, `<`, `<=`, `>`, `>=` overloaded for `date` and `time` — compile-time error to compare a `date` with a `time` or with any other type.

*Formatting:* `.format(fmt: string) -> string` — strftime-style format string. Common shorthand: `.iso() -> string` for ISO 8601 (`"2026-04-22"` / `"14:30:00"`).

**Example:**
```z
using datetime

fn main() -> int {
    let birthday: date = date(2000, 1, 15)
    let today_date: date = today()
    let age_days: int = today_date.diff(birthday)
    print(age_days)

    let appt: time = time(14, 30, 0)
    print(appt.hour())              # 14
    print(appt.format("%H:%M"))     # "14:30"

    let next_week: date = today_date.add_days(7)
    print(next_week.iso())
    return 0
}
```

**Runtime representation (`runtime/zdatetime.c`, `runtime/zdatetime.h`):**
```c
typedef struct ZDate {
    ZGCHeader header;
    int32_t   year;     // 1..9999
    int8_t    month;    // 1..12
    int8_t    day;      // 1..31 (month-aware)
    int16_t   _pad;
} ZDate;

typedef struct ZTime {
    ZGCHeader header;
    int32_t   hour;        // 0..23
    int32_t   minute;      // 0..59
    int32_t   second;      // 0..60
    int32_t   microsecond; // reserved for future precision; always 0 in v1
} ZTime;
```
Both types are GC-managed heap objects (consistent with `string` and every other reference type in the language). They're tiny (16-32 bytes), immutable, and arithmetic operations return new instances — same lifetime story as everything else, no special value-type machinery needed. Before M13 (the GC), `date`/`time` allocations leak alongside other heap objects; M13 makes them collectable.

**Implementation:**
- Add `datetime` to the compiler's library registry from M4. When `using datetime` is present, Sema injects `date`, `time`, `today`, `now`, and the comparison operators into scope.
- CodeGen recognizes `date(y, m, d)` and `time(h, m, s)` as special calls and lowers them to `z_date_new(y, m, d)` / `z_time_new(h, m, s)`. Method calls (`.year()`, `.add_days(n)`, `.format(s)`, etc.) lower to corresponding `z_date_*` / `z_time_*` runtime calls.
- Comparison operators are special-cased in CodeGen when both operands are `date` or both `time`: lower to `z_date_compare` / `z_time_compare` returning -1/0/1, then apply the requested ordering.
- The runtime uses libc's `<time.h>` for `today()` / `now()` (`time(NULL)`, `localtime_r`) and for `.format()` (`strftime`). No third-party dependency.
- Sema: reject any `date` / `time` reference without `using datetime` — same import-gating as every other library.

**Sema rules specific to this library:**
- Reject implicit conversions between `date` and `time` (they're distinct types with no shared semantics).
- Comparison between `date` and `time` is a compile error: `"cannot compare 'date' with 'time'"`.
- `date.add_days(0)` returns a fresh equal-valued `date` rather than reusing the input — keeps the "operations return new objects" invariant uniform.

**Mangler:** add type codes `dt` for `date` and `tm` for `time`.

**Test programs:**
- `tests/datetime/no_import.z` — uses `date(2026, 4, 22)` without `using datetime` → expect "unknown type 'date'"
- `tests/datetime/construct.z` — `date(2026, 4, 22)`, accessors, `.iso()` round-trip
- `tests/datetime/invalid_construct.z` — `date(2026, 13, 1)` → abort with clear message
- `tests/datetime/arithmetic.z` — `date.add_days(30).diff(date)` round-trip
- `tests/datetime/dow.z` — `date(2026, 4, 25).day_of_week()` → 6 (Saturday)
- `tests/datetime/today_now.z` — `today()` and `now()` produce reasonable values (smoke test, not exact)
- `tests/datetime/compare.z` — `date(2026, 1, 1) < date(2026, 12, 31)` → true; mixing `date` with `time` → compile error
- `tests/datetime/format.z` — `time(9, 5, 30).format("%H:%M:%S")` → `"09:05:30"`

### Milestone 9: `io` Library — File I/O

**Add:** the `io` standard library — reading and writing text files, JSON, Word documents, and Quarto markdown — imported via `using io`. Sits at M8 because text I/O requires only `string` (M3) and `dynamic` (M3) for flexible JSON values; no classes or containers needed for the basic API. Full JSON object support and `WordDoc` class expand in M12+ when classes land.

**Dependency note:** M8 ships a layered API. **Phase A** (M8, now) covers txt and raw JSON-as-string; **Phase B** (M10+, after structures) adds JSON-as-container (`unordered_map<string, dynamic>`); **Phase C** (M12+, after classes) adds the typed `WordDoc` and `QmdDoc` classes.

**API — Phase A (ships at M8):**
```z
using io

# Text files
let content: string  = io.read_text("notes.txt")
io.write_text("out.txt", content)
io.append_text("log.txt", "new line\n")
let exists: bool     = io.file_exists("data.csv")

# JSON — raw string in Phase A; parsed in Phase B
let raw: string      = io.read_json_raw("config.json")  # returns JSON as unparsed string
io.write_json_raw("config.json", raw)

# Word / QMD — text extraction only in Phase A
let text: string     = io.read_word_text("report.docx")  # extracts plain text
let qmd:  string     = io.read_qmd("report.qmd")         # reads as raw text
io.write_qmd("output.qmd", qmd)
```

**API — Phase B additions (M10, after `structures`):**
```z
# JSON as container — returns unordered_map<string, dynamic>
let cfg: unordered_map<string, dynamic> = io.read_json("config.json")
io.write_json("config.json", cfg)
```

**API — Phase C additions (M12, after classes):**
```z
# Typed Word document with structure
let doc: WordDoc     = io.open_word("report.docx")
let para: string     = doc.paragraph(0)
doc.append_paragraph("New content")
io.save_word(doc, "output.docx")
```

**Runtime (`runtime/zio.c`, `runtime/zio.h`):**
- Text files: standard C `fopen`/`fread`/`fwrite`/`fclose`; no external deps.
- JSON raw: read/write file as text — trivial; Phase B parser is a hand-written recursive-descent JSON parser in C (JSON grammar is small and well-defined).
- Word (.docx): OOXML is a ZIP archive containing XML files. Use **libzip** for ZIP handling and a minimal XML reader for paragraph extraction. CMake option `-DZ_ENABLE_WORD=ON` gates this; without it, `read_word_text` calls `abort()` with a clear message.
- QMD: plain text with YAML front matter — handled as string; no external deps.

**CMakeLists.txt additions:**
```cmake
option(Z_ENABLE_WORD "Enable Word (.docx) support (requires libzip)" OFF)
if(Z_ENABLE_WORD)
    find_package(libzip REQUIRED)
    target_link_libraries(zc PRIVATE libzip::zip)
    target_compile_definitions(zc PRIVATE Z_ENABLE_WORD)
endif()
```

**Test programs:**
- `tests/io/read_write_txt.z` — write a string, read it back, verify round-trip
- `tests/io/file_exists.z` — check existing and non-existing files
- `tests/io/json_raw.z` — read a JSON file as string, write it back
- `tests/io/qmd_roundtrip.z` — read QMD, append text, write back

### Milestone 10: `data` Library — DataFrames, CSV, Excel

**Add:** the `data` standard library — reading and wrangling tabular data (CSV, Excel) via a `DataFrame` API — imported via `using data`. Depends on `structures` (M10) for column storage and `io` (M8) for file reading. Full functionality ships after M12 (classes), since `DataFrame` and `Series` are classes. A text-parsing subset of CSV is available at M9 itself (pre-classes).

**Dependency note:** Phase A (M9, now) reads CSV into a raw `list<list<string>>` using string parsing. Phase B (M12+, after classes) provides the full `DataFrame` class API.

**API — Phase A (M9, plain parsing before classes):**
```z
using data

# Returns raw table as list of rows, each row a list of strings
let rows: list<list<string>> = data.read_csv_raw("sales.csv")
let header: list<string>     = rows.get(0)
let row1:   list<string>     = rows.get(1)
print(row1.get(2))   # print cell [1][2]
```

**API — Phase B (M12+, full DataFrame after classes):**
```z
using data

# CSV
let df: DataFrame = data.read_csv("sales.csv")
print(df.shape())                        # [1000, 5]
print(df.columns())                      # ["date", "product", "price", "qty", "region"]

# Excel
let xdf: DataFrame = data.read_excel("report.xlsx", "Sheet1")

# Column access
let prices: array<double> = df.column_double("price")
let names:  array<string> = df.column_string("product")

# Data wrangling
let filtered: DataFrame = df.filter("price > 100.0")
let sorted:   DataFrame = df.sort("price", false)      # false = descending
let top10:    DataFrame = df.head(10)
let bottom5:  DataFrame = df.tail(5)

# Aggregations
let avg_price: double   = df.mean("price")
let total_qty: double   = df.sum("qty")
let grouped:   DataFrame = df.groupby("region").mean()

# Summary
df.describe()   # prints min, max, mean, std, count per numeric column
df.info()       # prints column names, types, null counts

# Write
data.write_csv(df, "output.csv")
data.write_excel(df, "output.xlsx", "Results")
```

**`DataFrame` class (defined in `stdlib/data.z` — self-hosted):**
```z
class DataFrame {
    let columns_: list<string>
    let data_:    unordered_map<string, array<dynamic>>

    DataFrame(): columns_ = list<string>(), data_ = unordered_map<string, array<dynamic>>() { }
    fn shape()   -> array<int> { ... }
    fn columns() -> list<string> { ... }
    fn column_double(name: string) -> array<double> { ... }
    fn filter(expr: string) -> DataFrame { ... }
    fn sort(col: string, ascending: bool) -> DataFrame { ... }
    fn head(n: int) -> DataFrame { ... }
    fn mean(col: string) -> double { ... }
    fn groupby(col: string) -> GroupBy { ... }
    fn describe() -> void { ... }
}
```

**Runtime (`runtime/zdata.c`):**
- CSV parsing: RFC 4180 compliant parser — handles quoted fields, newlines in fields, custom delimiters. Written in C, no external deps.
- Excel (.xlsx): OOXML ZIP + XML, same mechanism as Word in `io`. Gated behind `-DZ_ENABLE_EXCEL=ON` and libzip.
- Filter expressions: a mini expression evaluator parsing strings like `"price > 100.0"` — compares column values against literals. Full expression language is future work.

**CMakeLists.txt:**
```cmake
option(Z_ENABLE_EXCEL "Enable Excel (.xlsx) support (requires libzip)" OFF)
if(Z_ENABLE_EXCEL)
    find_package(libzip REQUIRED)
    target_link_libraries(zc PRIVATE libzip::zip)
    target_compile_definitions(zc PRIVATE Z_ENABLE_EXCEL)
endif()
```

**Test programs:**
- `tests/data/csv_raw.z` — read CSV with `read_csv_raw`, verify row/column count
- `tests/data/csv_df.z` (M12+) — `read_csv` into DataFrame, verify shape, columns
- `tests/data/filter_sort.z` (M12+) — filter + sort, verify result rows
- `tests/data/groupby_mean.z` (M12+) — groupby region, verify mean per group
- `tests/data/describe.z` (M12+) — describe on numeric columns, spot-check values
- `tests/data/write_csv.z` (M12+) — write DataFrame to CSV, read back, verify round-trip

### Milestone 11: `structures` Library

**Add:** the `structures` standard library — `array<T>` (fixed-size GC-managed sequence), `vector<T>` (dynamic array with doubling growth), `list<T>` (doubly-linked list), `stack<T>`, `queue<T>`, `heap<T>` (min-heap / priority queue), `bstree<T>` (binary search tree), `map<K, V>` (ordered map, red-black tree), `unordered_map<K, V>` (hash map) — imported via `using structures`.

**Strategy — compiler-known generic types backed by a small C runtime.** Rather than implementing a full generic/template system, treat `array<T>` / `vector<T>` / `map<K,V>` / etc. as *special-cased* in Sema and CodeGen, gated on `using structures`. The compiler knows their method sets and generates calls to a small runtime library.

**Sub-phases (each independently shippable):**
- **M10a — sequences:** `array<T>` (fixed-size, bounds-checked), `vector<T>` (dynamic array, doubling growth), `list<T>` (doubly-linked list). Sequences land first because every other container either builds on them or is exercised through them. Both `vector` (random access, cache-friendly) and `list` (O(1) splice/insert at known position, no random access) ship together so users learn the trade-off explicitly.
- **M10b — LIFO/FIFO/heap:** `stack<T>`, `queue<T>`, `heap<T>`. Built on top of `vector<T>` (stack and heap as resizable arrays; queue as a circular buffer over a vector).
- **M10c — associative + BST:** `bstree<T>` (BST with no auto-balancing), `map<K, V>` (red-black tree, sorted iteration, O(log n) ops), `unordered_map<K, V>` (open-addressing hash table, O(1) average ops). All three exercise the GC's pointer-bitmap walking through linked nodes / hash buckets — the most thorough test of the GC before classes (M12) need it.

**`vector<T>` vs `list<T>` — when to use which:**
- `vector<T>` — random access via `.get(i)`, push/pop at the end is amortized O(1), insertion in the middle is O(n). The default sequential container; pick this unless you know you need `list`.
- `list<T>` — random access is O(n), but insertion/removal at a known position (front, back, or via an iterator) is O(1). Picks up where `vector` is bad: long sequences with frequent middle-insertions.

**Runtime API (`runtime/zruntime.c`):**
- `array<T>` — `z_array_new(n, elem_size) -> ZArray*`, `z_array_get(arr, i) -> void*`, `z_array_set(arr, i, val_ptr)`, `z_array_size(arr) -> i64`, `z_array_fill(arr, val_ptr)`. Bounds-checked at runtime with `abort()`.
- `vector<T>` — `z_vector_new(elem_size)`, `z_vector_push`, `z_vector_pop`, `z_vector_get`, `z_vector_set`, `z_vector_size`, `z_vector_reserve`, `z_vector_clear`. Dynamic array with doubling growth (×2 on overflow, ÷2 reclaim threshold).
- `list<T>` — `z_list_new(elem_size)`, `z_list_push_front`, `z_list_push_back`, `z_list_pop_front`, `z_list_pop_back`, `z_list_front`, `z_list_back`, `z_list_size`, `z_list_empty`. Doubly-linked list of `ZListNode { prev, next, value_inline }` cells. Each node is a separate GC allocation; node typeinfo declares `prev`/`next` as non-GC pointers (raw struct pointers within the same list, walked via `z_list` traversal — the list head's typeinfo is what the GC actually traces).
- `stack<T>` — `z_stack_new`, `z_stack_push`, `z_stack_pop`, `z_stack_top`, `z_stack_size`, `z_stack_empty`. Wraps a `vector` internally — push/pop at the back.
- `queue<T>` — `z_queue_new`, `z_queue_push`, `z_queue_pop`, `z_queue_front`, `z_queue_size`, `z_queue_empty`. Circular buffer over a `vector`.
- `heap<T>` — `z_heap_new(elem_size, compare_fn)`, `z_heap_push`, `z_heap_pop`, `z_heap_top`, `z_heap_size`, `z_heap_empty`. Binary min-heap stored in a `vector`.
- `bstree<T>` — `z_bstree_new(compare_fn)`, `z_bstree_insert(tree, val)`, `z_bstree_find(tree, val) -> bool`, `z_bstree_remove(tree, val)`, `z_bstree_size(tree)`, plus root-node accessors `.left()` / `.right()` / `.value()` for traversal. No auto-balancing in v1 — degenerate inputs produce O(n) chains. AVL / red-black auto-balancing is a Ceiling escape path.
- `map<K, V>` — `z_map_new(key_size, val_size, compare_fn)`, `z_map_insert(map, key_ptr, val_ptr)`, `z_map_get(map, key_ptr) -> void*` (or null on miss), `z_map_remove(map, key_ptr) -> bool`, `z_map_contains`, `z_map_size`, `z_map_clear`. Implemented as a **red-black tree** so insertion/lookup/removal are O(log n) with bounded-depth guarantees, and iteration yields keys in sorted order. The single instance where v1 ships a balanced tree (rather than a leaving auto-balancing as a Ceiling) — `map` is the user-facing ordered associative container, so degenerate O(n) behavior would be unacceptable.
- `unordered_map<K, V>` — `z_umap_new(key_size, val_size, hash_fn, eq_fn)`, `z_umap_insert`, `z_umap_get`, `z_umap_remove`, `z_umap_contains`, `z_umap_size`, `z_umap_clear`. Open-addressing hash table with Robin Hood probing and load factor ≤ 0.75. Builtin hash/equality for primitive key types (`int`, `string`); for user-class keys, requires the comparator/hash-overload story from M15.

**`array<T>` layout (the canonical container layout):**
```c
struct ZArray {
    ZGCHeader header;     // typeinfo carries the element pointer-bitmap
    int64_t   size;       // element count
    uint8_t   elements[]; // size * sizeof(T) bytes inline (flex-array tail)
}
```
`array<T>(n)` lowers to `z_array_new(n, sizeof(T))` which calls `z_gc_alloc(sizeof(ZArray) + n * sizeof(T), &Array_T_typeinfo)`. The compiler emits a typeinfo per element type — `array<string>` / `array<Point>` typeinfo says "every element slot is a GC pointer," so marking traces through the array transitively. For `array<int>` the bitmap is empty.

**`vector<T>` layout:**
```c
struct ZVector {
    ZGCHeader header;       // typeinfo bitmap covers the buffer's element slots
    int64_t   size;         // current element count
    int64_t   capacity;     // allocated slot count
    void*     buffer;       // separately-allocated payload (z_gc_alloc_bytes for primitive, GC-typed for class elements)
}
```

**`list<T>` layout (doubly-linked):**
```c
struct ZListNode {
    ZGCHeader  header;      // typeinfo declares prev/next/(value_ptr if T is a class) as GC fields
    ZListNode* prev;
    ZListNode* next;
    /* value follows inline: T value; — flex tail for value-types */
}
struct ZList {
    ZGCHeader  header;      // typeinfo declares head/tail as GC pointers
    ZListNode* head;
    ZListNode* tail;
    int64_t    size;
}
```

**`map<K, V>` layout — red-black tree node:**
```c
struct ZMapNode {
    ZGCHeader  header;
    ZMapNode*  parent;
    ZMapNode*  left;
    ZMapNode*  right;
    int8_t     color;       // RED or BLACK
    /* key + value follow inline, sized by key_size and val_size */
}
```
Standard CLRS red-black tree algorithms (insert/delete with rotations) implemented in C runtime once. The `map` typeinfo declares `parent`/`left`/`right` and (if `K` or `V` is a class type) the key/value pointer offsets as GC fields — marking traces the whole tree.

**`unordered_map<K, V>` layout — open-addressing buckets:**
```c
struct ZUMap {
    ZGCHeader header;
    int64_t   size;
    int64_t   capacity;     // power of 2
    void*     buckets;      // ZUMapBucket[capacity] — separately allocated
}
struct ZUMapBucket {
    int32_t state;          // EMPTY | FILLED | TOMBSTONE
    int32_t probe_distance; // for Robin Hood
    /* key + value inline */
}
```
Hash function: FNV-1a for `int`/primitive keys, byte-wise FNV-1a for `string`. Resizes (doubling) when load factor crosses 0.75.

**CodeGen:** `vector<int>()` → `z_vector_new(sizeof(int))`. `xs.push(5)` → store `5` in a temp alloca, call `z_vector_push(xs, &temp)`. `xs.size()` → direct call to `z_vector_size(xs)`. Same pattern for every container. For `map<string, int>`, the key-write needs a temp alloca for the string pointer plus a temp alloca for the int value.

**Sema:** type-check element types against the container's generic parameter. `vector<int>::push` takes `int`, not `double`. For maps, the key type must be one the runtime can hash/compare — primitives (`int`, `string`) for v1; user classes require M15 overloading. Reject any use of `array<T>` / `vector<T>` / `map<K,V>` / etc. unless `using structures` is present in the file.

**Linking:** CMake builds `runtime/zruntime.c` into a static lib `libzruntime.a` that the driver links into every compiled program.

**Test programs:**
- `tests/structures/no_import.z` — uses `vector<int>` without `using structures` → expect error
- `tests/structures/array_basic.z` — `array<int>([1,2,3,4,5])`, `.get(i)`, `.size()`, sum
- `tests/structures/array_of_strings.z` — `array<string>` round-trip; verify the GC traces through the array (run under `--gc-stress`)
- `tests/structures/array_bounds.z` — `xs.get(99)` aborts with a clear message
- `tests/structures/vector_round_trip.z` — push/pop/get/size on `vector<int>`; trigger growth past the initial capacity
- `tests/structures/list_round_trip.z` — push_front/push_back/pop on `list<int>`; verify doubly-linked invariants by walking forward and backward
- `tests/structures/stack_queue_heap.z` — push/pop/top/size sanity checks
- `tests/structures/bstree_search.z` — insert into `bstree<int>`, find/remove, verify O(h) behavior
- `tests/structures/map_ordered.z` — `map<string, int>` insert/get/remove; iterate and verify keys come out sorted
- `tests/structures/umap_basic.z` — `unordered_map<string, int>` insert/get/remove; force load-factor crossing to trigger resize
- `tests/structures/umap_collisions.z` — adversarial insert pattern designed to cause collisions; verify correctness under Robin Hood probing

### Milestone 12: `algorithms` Library

**Add:** the `algorithms` standard library — sorting, searching, and BST-traversal algorithms over `structures` containers — imported via `using algorithms`. Depends on `structures` (M10); independent of OOP and tensor.

**Why a separate library and a separate milestone:** algorithms naturally belong in a layer above containers but below OOP — they don't need classes, they don't need overloading (free functions on existing container types), and they don't need the tensor stack. Sitting between M10 and the OOP cluster gives the compiler a real test of cross-library composition (`algorithms` calls into `structures`'s C runtime) before the OOP retrofit lands in M12/M13.

**Self-hosted in `stdlib/algorithms.z`** — same architectural principle as `regression`. The library lives as ordinary Z source code, parsed and type-checked through the normal pipeline. The compiler does not synthesize free functions in C++; the source file is where users can grep, debug, and step through.

**API:**

*Sorting* — in-place on `vector<T>` and `array<T>`:
- `bubble_sort(xs)` — O(n²), educational baseline
- `insertion_sort(xs)` — O(n²) but fast on small / nearly-sorted inputs
- `merge_sort(xs)` — O(n log n) stable, recursive
- `quick_sort(xs)` — O(n log n) average, Lomuto partition
- `heap_sort(xs)` — O(n log n) using `heap<T>` from `structures`

All sort variants have an overload that takes a comparator: `quick_sort(xs, compare)` where `compare` is a function value of type `(T, T) -> int` returning -1/0/1. Function values land in M15 (overloading) territory; this overload is added then.

*Searching*:
- `linear_search(xs, target) -> int` — returns index or -1
- `binary_search(xs, target) -> int` — requires a sorted sequence; returns index or -1

*BST traversal* — over `bstree<T>` from M10c:
- `in_order(tree, visit)`  — left, root, right (yields sorted sequence for a BST)
- `pre_order(tree, visit)` — root, left, right
- `post_order(tree, visit) ` — left, right, root
- `level_order(tree, visit)` — breadth-first across levels (uses a `queue<Node*>` from M10b)

`visit` is a function value of type `(T) -> void`; lands when M15 enables function values.

**Implementation strategy:**
- All sorting and searching is written in pure Z on top of `list<T>` / `array<T>` from `structures`. No new C runtime code.
- Recursive algorithms (`merge_sort`, `quick_sort`, tree traversals) work the moment recursion does — no extra plumbing.
- Comparator-taking overloads are gated behind M15 (overloading + function values). Until then, the library ships only the default-comparator versions for built-in numeric types.

**Sema rules specific to this library:**
- Reject `bubble_sort` / `in_order` / etc. without `using algorithms` — normal import-gating.
- Sort and search functions require their argument's element type to support comparison. For built-in numerics this is automatic; for user types it'll require the comparator overload (M15).

**Test programs:**
- `tests/algorithms/no_import.z` — `bubble_sort(xs)` without `using algorithms` → expect error
- `tests/algorithms/sort_int.z` — every sort variant on the same input, verify identical output
- `tests/algorithms/sort_strings.z` — alphabetic sort of `vector<string>`
- `tests/algorithms/binary_search.z` — verify correct index on hits and -1 on misses
- `tests/algorithms/bstree_inorder.z` — in-order traversal of a BST yields a sorted sequence
- `tests/algorithms/bstree_level_order.z` — level-order traversal of a 3-level BST; verify visit sequence
- `tests/algorithms/sort_stress.z` — sort a 10K-element vector under `--gc-stress` (validates that recursive merge_sort doesn't break the GC's shadow-stack invariants)

### Milestone 13: Classes (leaky)

**Add:** `class` declarations with fields, instance methods, constructors (with init lists), `new` expression, `self` reference, member access with `.`. **No destructors.** Heap objects allocated by `new` are `malloc`'d directly and leak for the lifetime of the process — that's fine; this milestone's job is to prove the class system works end-to-end. Object lifetime lands in M13.

**Constructor syntax:**
```z
class Point {
    let x: double
    let y: double

    Point(xv: double, yv: double): x = xv, y = yv { }
}
```
The initializer list can contain:
- A single `super(args)` call (only valid when the class has a parent — full treatment deferred to M14) — runs first
- Zero or more `fieldName = expr` initializers — run in declaration order of fields

Code in the `{ body }` runs after the initializer list completes.

**Files:**
- Extend `src/Token.h`: TK_Class, TK_New, TK_Self, TK_Dot, TK_Void
- Extend `src/AST.h`: `ClassDecl { name, fields, methods, ctor }`, `FieldDecl`, `MethodDecl`, `CtorDecl { name, params, initList, body }`, `InitItem { kind: Super|Field, args|fieldName+expr }`, `NewExpr`, `MemberAccessExpr`, `SelfExpr`
- Extend `src/ZType.h`: `ZTypeKind::Class` with pointer to `ClassDecl`; a `ClassInfo` struct containing field layout (name → offset) and method table
- Extend `src/Parser.cpp`: `parseClassDecl`, `parseCtorDecl` (detect ctor by `IDENT == className` before `(`), `parseInitList`, `parseNewExpr`, member access in primary expressions
- Extend `src/Sema.cpp`:
  - Class table, field resolution, `self` binding inside methods
  - Ensure `new ClassName(args)` matches constructor signature
  - Validate that init-list field names exist on the class and types match
  - Forbid `super(...)` in a class that has no parent
- Extend `src/CodeGen.cpp`:
  - Each class becomes an `llvm::StructType` using the canonical layout: `[ZGCHeader, <parent fields>, <own fields>]`. The `ZGCHeader` is present from day one but its fields (`typeinfo`, `mark_flags`) are zero-initialized and unused — M13 turns them on. This commits the layout so M13 is a pure runtime swap, not a layout migration.
  - Each method becomes a free function named `ClassName__methodName` with an implicit first param `self: ClassName*`
  - Constructor codegen order inside the function body:
    1. If init list has `super(args)` — emit call to parent ctor first
    2. For each field in declaration order: if there is an init-list entry, store that value; else zero-initialize
    3. Emit the ctor body statements
  - `new ClassName(args)` generates: `malloc(sizeof(ClassName))` → zero the `ZGCHeader` → call constructor → return pointer. No `free` anywhere; objects leak.
  - `obj.field` → GEP past the GC header to the named field; `obj.method(args)` → call `ClassName__method(obj, args)` (non-virtual for now)

**End state:** `Point p = new Point(1.0, 2.0); print(p.distance_to(new Point(4.0, 6.0)))` compiles, runs, prints the right answer, leaks both `Point`s on exit. Test programs verify the class *surface* works; leak testing is deferred to M13.

**Test program:** `Point` class with `x`, `y` fields, `distance()` method, constructor using init list `Point(xv: double, yv: double): x = xv, y = yv { }`.

### Milestone 14: Garbage Collector + Destructors

**Add:** a tracing garbage collector that owns every heap allocation, plus `~ClassName()` destructor syntax that runs as a finalizer during collection. After this milestone, no heap object in the program leaks.

**Scope gate — what M13 *does not* do:** no generational collection, no incremental/concurrent marking, no compaction, no write barriers (they're not needed for a non-moving stop-the-world collector). Plain mark-and-sweep, one heap, one collector thread. Everything fancy goes in the Ceilings & Escape Paths appendix for later.

**Destructor syntax (new):**
```z
class Logger {
    let file_handle: int    # raw OS handle — NOT a GC-managed field

    Logger(h: int): file_handle = h { }

    ~Logger() {
        # runs when the GC reclaims this Logger
        close(self.file_handle)
    }
}
```
A class may declare at most one destructor, named `~ClassName`, no parameters, no return type. Destructors are **finalizers**: the collector calls them at reclaim time, not at scope exit — this language has no RAII. They exist to release non-memory resources (file handles, sockets, native buffers), not to manipulate other GC objects.

**Destructor body restrictions (Sema-enforced):**
To keep finalizer behavior defined, M13 Sema rejects destructor bodies that:
1. Contain a `new` expression — finalizers must not grow the heap or resurrect references.
2. Read or write any field whose static type is a class, tensor, string, or container. Finalizer ordering across a reachable set is undefined; touching another GC-managed field means observing maybe-already-finalized objects.
3. Call Z methods or Z functions. (Method calls transitively violate rule 2 and would re-open the loophole.)
4. Call anything outside a small allow-list of C runtime functions: `printf`, `fprintf`, `close`, `fclose`, `free`, plus user-declared `extern "C"` functions the FFI marks as finalizer-safe.

The compile error is specific: `"destructor of Node cannot access GC-managed field 'next' of type Node* — finalizer ordering across GC objects is undefined; move the cleanup to an explicit close() method"`. Users who want determinism write a `close()` method and call it manually — that's a known limitation, noted in the Ceilings appendix.

**Root scanning — shadow stack, not LLVM stack maps:**
The compiler maintains a thread-local shadow stack of GC roots. At every function entry, CodeGen emits a push for each local whose static type is GC-managed; at function exit (and at block exit for locals declared inside a nested block), it emits matching pops. The collector's root phase just walks this stack — it never touches native frames or LLVM statepoints.
```
  // At function entry, for each GC-managed local `p`:
  %p = alloca %Point*
  call void @z_gc_push_root(i8** bitcast(%Point** %p to i8**))
  ...
  // At every return path:
  call void @z_gc_pop_root()
  ret ...
```
Rules:
- Push order at entry == reverse pop order at exit (LIFO). Early returns pop everything the function pushed.
- `alloca` happens first; `push_root` happens after — so the slot is always live memory when the collector reads it.
- Fields of GC-managed objects are not on the shadow stack — they're reached transitively via the `typeinfo` pointer bitmap during marking.
- Cross-function temporaries (e.g. `new Foo(new Bar())`) are handled by pushing the inner allocation as an anonymous root before the outer allocation call, popping after.

This is the whole root-scanning story. No LLVM `gc.statepoint`, no frame walking, no stack map format. ~40 lines of CodeGen and ~20 lines of runtime. Replace-with-LLVM-statepoints is an item in Ceilings & Escape Paths.

**Root-tracking conventions for the four standard cases.** These follow the Java/C# bytecode-VM model adapted to a shadow stack:

1. **Function parameters.** A GC-managed parameter is, from the callee's perspective, just another local. The callee's prologue emits `z_gc_push_root` for every GC-managed parameter slot in declaration order, immediately after copying the incoming argument into its own `alloca`. The caller separately keeps the value rooted on its own shadow stack until the call returns — the ABI does not assume the callee will root anything. Both sides root independently; this matches how Java's interpreter pushes args onto the operand stack while the callee's local-variable table also holds them.

2. **Return values.** Caller-owned, caller-rooted. Before emitting the `call`, CodeGen allocates a return slot (`%ret = alloca T*`) and pushes it onto the shadow stack. The call writes its return value into that slot — implemented as a normal LLVM SSA return for primitives, but for GC-managed return types the caller passes a hidden "return slot pointer" parameter and the callee stores through it before its own pops run. This is exactly the C# JIT convention for byref returns and removes the "return value briefly unrooted in a register" race.

3. **`z_gc_alloc_bytes` for raw payloads.** Any allocation that holds no Z-typed pointers (tensor `data`, `shape`, `strides`; container payload buffers; the trailing bytes of `ZArray`) goes through `z_gc_alloc_bytes(size)`, which is `z_gc_alloc(size, &z_bytes_typeinfo)` where `z_bytes_typeinfo` is a singleton declaring an empty pointer bitmap and no destructor. The collector marks the block as live (preventing reclaim while a parent object references it) but does not recurse into its contents. This is the equivalent of a Java `byte[]` or a `GC_malloc_atomic` block in the Boehm collector.

4. **Autograd tape registration.** The tape is itself a GC-allocated object — a `list<ZAutogradNode*>` with the standard typeinfo declaring "every slot is a GC pointer." Adding a `ZAutogradNode*` to the tape is a normal field write into a GC object; the marker reaches it during the next collection by walking the tape's bitmap. No `z_gc_register_root_array` call is needed because the tape is already reachable from the executing function's locals, and its contents are reachable from the tape via standard tracing. `.backward()` clears the tape (sets length to 0); the next collection reclaims the now-unreferenced nodes. This is the same pattern PyTorch uses internally — the tape is a regular Python list, not a special root set.

5. **Nested generic typeinfo (e.g. `list<list<int>>`, `array<Point>`).** Typeinfo emission is recursive on the type structure. CodeGen maintains a memo table `Map<ZType, GlobalVariable*>` keyed by the canonical type. To emit typeinfo for `list<list<int>>`:
   - Recurse: emit typeinfo for `list<int>` first if not already in the table. Its element typeinfo points to `int_typeinfo` (empty bitmap).
   - Emit typeinfo for `list<list<int>>` whose element bitmap says "1 GC pointer per slot, pointing at a `list<int>`," with the element typeinfo set to `list_int_typeinfo`.
   - For arrays: `array<Point>` typeinfo says "GC pointer at every offset within the trailing payload, stride = sizeof(Point*)," with element typeinfo `Point_typeinfo`.
   This is what C#'s `RuntimeTypeHandle` / Java's per-instantiation class objects do under the hood; we just emit them statically at compile time.

**GC runtime (`runtime/zgc.c` + `runtime/zgc.h`):**
- **Allocation:** free-list per size class, with a large-object fallback. `z_gc_alloc(size, typeinfo)` returns a zeroed block whose `ZGCHeader` has `typeinfo` set and `mark_flags` cleared. Allocation triggers a collection when bytes-since-last-gc crosses a threshold.
- **Marking:** recursive DFS from each shadow-stack root. At each object, consult its `typeinfo` bitmap to find pointer-valued fields and recurse. Set `mark_flags = MARKED` to guard against cycles (this is the feature that refcounting can't match).
- **Sweeping:** walk every allocated block. Unmarked blocks: call `typeinfo->destructor(self)` if non-null, then return memory to the free list. Marked blocks: clear the mark bit for the next cycle.
- **Safepoints:** for v1, the only safepoint is the `z_gc_alloc` call itself — collection can only trigger at allocation. This removes any need for `z_gc_poll()` at loop back-edges and means user code runs without any polling overhead. A long allocation-free loop can delay GC, which is acceptable for a learning compiler; the escape path is to add back-edge polls later if needed.
- **Debug hooks:** `--gc-trace` dumps every allocation and collection; `--gc-stress` runs a collection on every allocation (invaluable for shaking out missing shadow-stack pushes).

**Language work this milestone adds (on top of M12):**
- Extend `src/Token.h`: TK_Tilde (`~`)
- Extend `src/AST.h`: `DtorDecl { body }`; add `dtor` slot to `ClassDecl`
- Extend `src/Parser.cpp`: `parseDtorDecl` (triggered by `~` followed by class name). Reject more than one destructor per class.
- Extend `src/Sema.cpp`:
  - The four finalizer-body restrictions above — walk every expression/statement in a `~ClassName()` body and reject violations with specific messages.
  - Track which locals are GC-managed so CodeGen can emit shadow-stack push/pop in the right places.
- Extend `src/CodeGen.cpp`:
  - Emit a `@ClassName_typeinfo` global containing: pointer-offset bitmap, destructor function pointer (or null), parent typeinfo pointer, object size
  - If the class has a destructor, emit `ClassName__dtor(self: ClassName*)`. Its body is: user destructor statements, then tail-call to `ParentClass__dtor(self)` if a parent dtor exists. `@ClassName_typeinfo.destructor` points to this function.
  - Swap `new`'s allocation path: `malloc(sizeof(C))` → `z_gc_alloc(sizeof(C), @ClassName_typeinfo)`.
  - Emit shadow-stack push/pop around every GC-managed local and anonymous temporary.

**Cross-milestone retrofit — `structures` onto the GC:**
The C runtime for list/stack/queue/heap from M10 currently uses `malloc` for its control structures and payload buffers. M13 refactors it:
- Every container allocates its control struct and payload buffer through `z_gc_alloc` / `z_gc_alloc_bytes`.
- Container constructors now take an extra hidden argument: a `typeinfo*` for the element type, supplied by CodeGen at the call site. `list<Point>()` passes `@Point_typeinfo`; `list<int>()` passes a compiler-emitted `@int_typeinfo` whose pointer bitmap is empty.
- The payload buffer's `typeinfo` declares the element stride and whether elements contain GC pointers, so marking recurses correctly into container contents.
- For primitive element types, the bitmap is trivially empty — marking stops at the payload buffer. For class-typed elements, marking walks each slot using the element typeinfo.

String literals are flagged "always reachable" in their header (or stored in a separate immortal region) so they never get collected even when no shadow-stack root points to them.

**Test programs:**
- `tests/gc/leak_smoke.z` — allocate 1M `Point`s in a loop; verify RSS stays bounded (the collector is running)
- `tests/gc/destructor_runs.z` — class with `~Logger()` that prints a tag; allocate + drop references + force collection; verify tag printed
- `tests/gc/cycle.z` — two-node cycle (`a.other = b; b.other = a`), drop both references, force collection; verify memory reclaimed (the killer feature vs refcounting)
- `tests/gc/finalizer_rules.z` — five deliberately-broken destructor bodies (accesses GC field, calls method, calls `new`, calls non-allow-listed function, throws) → each must produce a specific Sema error
- `tests/gc/stress.z` — compile with `--gc-stress`; exercise classes + containers together with no leaks or use-after-free
- `tests/gc/container_elements.z` — `list<Point>` where the Points have destructors; verify finalizers run when the list is unreachable

### Milestone 15: Inheritance, Interfaces, and Virtual Dispatch

**Add:** single class inheritance, `interface` declarations, `virtual`/`override` methods, polymorphic upcast, vtable-based dispatch.

**Files:**
- Extend `src/Token.h`: TK_Interface, TK_Virtual, TK_Override, TK_Super, TK_Colon (already present)
- Extend `src/AST.h`: `InterfaceDecl { name, methodSigs }`, add `parentName`, `implementedInterfaces`, `isVirtual`, `isOverride` to relevant nodes, `SuperCallExpr` for `super(args)` in constructors
- Extend `src/Sema.cpp`:
  - Build inheritance DAG — detect cycles
  - Verify `override` methods match a parent's `virtual` method signature exactly
  - Verify classes implementing an interface define every method in the interface (same signatures)
  - Allow implicit upcast from `Derived*` to `Base*` / `Interface*` in assignments and parameter passing
  - Field layout rule: the canonical layout (`[ZGCHeader, vtable_ptr?, parent fields, own fields]`) means a derived class struct shares its prefix with its parent — upcasting is a no-op pointer reinterpret. The `vtable_ptr` slot is inserted right after `ZGCHeader` for any class that participates in polymorphism; classes without virtual methods skip it.
- Extend `src/CodeGen.cpp`:
  - **Vtable generation:** for each class with any virtual methods, emit a global constant array of function pointers `@ClassName_vtable = [fnPtr1, fnPtr2, ...]` in method-table order
  - For any class that has virtual methods or inherits from one, reserve the `vtable_ptr` slot in the struct layout (between `ZGCHeader` and the first field). See the canonical layout in OOP — Memory layout.
  - Constructor sets `self->vtable_ptr = &ClassName_vtable` after the `super(args)` call (if any) and before user field initializers — so the object has the right vtable from the moment any of its own code runs.
  - Method-table order is inherited — a derived class's vtable has the same slot order as its parent, with overridden slots pointing to the derived method's function
  - **Virtual call dispatch:** `obj.virtualMethod(args)` → load `vtable_ptr` from `obj` → GEP to method's slot → load function pointer → indirect call
  - **Non-virtual call:** direct call to mangled name (already in M12)
  - **Interface dispatch:** each interface gets its own vtable layout; class implementing `N` interfaces stores `N` interface-vtable pointers (simplest approach) OR use a fat-pointer `{ dataPtr, ifaceVtablePtr }` — choose fat pointers for interface values, regular pointers for class values
  - **`super(args)` call in constructor:** generates call to parent's constructor with `self` before running own body

**Test program:** `Shape` → `Circle`, `Rectangle` with `virtual area()`; `Drawable` interface; verify polymorphism by holding a `Shape*` that points to `Circle` and calling `.area()`.

### Milestone 16: Function and Method Overloading

**Add:** multiple functions/methods with the same name distinguished by parameter types. Implemented via **name mangling**.

**Files:**
- Extend `src/Sema.cpp`: function/method lookup becomes a set keyed by `(name, paramTypes)`. On call site `foo(args)`, resolve the best match by exact type. No implicit conversions in the overload set for the learning version (either exact match or error).
- Extend `src/CodeGen.cpp`: mangle every function name to include parameter types, e.g. `max(int, int)` → `max__i64i64`, `max(float, float)` → `max__f32f32`, `max(float16, float16)` → `max__f16f16`, `Circle::draw()` → `Circle__draw__v`. Simple mangling scheme: `name__<paramCodes>` where codes are `i32`/`i64`/`i128` integers, `f16`/`f32`/`f64` floats, `b` bool, `ch` char, `s` string, `P<ClassName>` for class pointers.
- Mangling must be applied consistently in both the function definition and the call site — a single `ZMangler` utility class centralizes the logic.
- Virtual dispatch still works: each mangled overload gets its own vtable slot if virtual.

**Test programs:**
- `fn print_val(x: int)` and `fn print_val(x: float)` — two overloads
- Class `Writer` with `write(x: int)` and `write(s: string)` — method overloading

**Addendum — backfill M6 `math` integer overloads:**
- Add `abs(int)`, `abs(int64)`, `min(int, int)`, `min(int64, int64)`, `max(int, int)`, `max(int64, int64)` to the `math` library.
- Integer versions are emitted inline as compare + select in LLVM IR; double versions keep calling libm.
- `tests/math/int_overloads.z` — `max(3, 7)` now dispatches to the int version; `max(1.5, 2.5)` still dispatches to the double version.

### Milestone 17a: MLIR Foundation — Build Integration and the `z` Dialect

**Add:** MLIR as a second IR path inside the compiler, proven end-to-end on one trivial operation. No tensor semantics land here. This milestone exists so that when M17b goes wrong, the failure is a tensor bug and not a build/pipeline/version bug.

**Prerequisite (do this first, as its own commit):** install MLIR and take the LLVM version bump it forces (see the prerequisite section at the top of this document). Verify `zc` still builds and every earlier milestone's example still runs *before* writing any MLIR code. An LLVM major-version bump touching `CodeGen.cpp` and `main.cpp` at the same time as new MLIR work is the single most likely way to lose a week here.

**Build integration:**
- `option(Z_ENABLE_MLIR "Build the MLIR tensor backend" OFF)` — everything in this milestone is behind it, and `Src/MLIR/*` is only added to the target when it is `ON`.
- `find_package(MLIR REQUIRED CONFIG)`, then `list(APPEND CMAKE_MODULE_PATH "${MLIR_CMAKE_DIR}" "${LLVM_CMAKE_DIR}")` and `include(AddMLIR)` / `include(TableGen)`.
- `mlir_tablegen(ZOps.h.inc -gen-op-decls)` / `(ZOps.cpp.inc -gen-op-defs)` / `(ZDialect.h.inc -gen-dialect-decls)` over `ZDialect.td`, wrapped in `add_public_tablegen_target(ZDialectIncGen)` and added as a dependency of `zc`.
- Link `MLIRIR MLIRParser MLIRSupport MLIRPass MLIRTransforms MLIRFuncDialect MLIRArithDialect MLIRSCFDialect MLIRMemRefDialect MLIRLinalgDialect MLIRTensorDialect MLIRBufferizationDialect MLIRLLVMDialect MLIRToLLVMIRTranslationRegistration MLIRTargetLLVMIRExport`.

**The `z` dialect (`Src/MLIR/ZDialect.td`):** deliberately tiny. It exists to hold Z-level semantics that have no faithful `linalg` spelling *at the point of emission* — not to be a full tensor IR. In M17a it defines exactly one op:

```tablegen
def Z_AddOp : Z_Op<"add", [Pure, SameOperandsAndResultType]> {
  let arguments = (ins AnyRankedTensor:$lhs, AnyRankedTensor:$rhs);
  let results   = (outs AnyRankedTensor:$result);
}
```

Later milestones add `z.broadcast_binop`, `z.matmul`, `z.reduce`, `z.device_to` — each with a documented lowering. **Rule: an op earns a place in the `z` dialect only if lowering it eagerly to `linalg` at emission time would lose information a later pass needs.** Anything else is emitted as `linalg`/`arith`/`tensor` directly. A bloated custom dialect is how this decision goes wrong.

**The pipeline (`Src/MLIR/Pipeline.cpp`):** the lowering chain M17b–M22 all build on, established here for one op.

```
z dialect
  -> linalg on tensor          (ZToLinalg conversion pass — your code)
  -> linalg fusion             (-linalg-fuse-elementwise-ops)
  -> bufferization             (-one-shot-bufferize="bufferize-function-boundaries")
  -> linalg on memref
  -> loops                     (-convert-linalg-to-loops)
  -> scf -> cf                 (-convert-scf-to-cf)
  -> llvm dialect              (-finalize-memref-to-llvm, -convert-func-to-llvm,
                                -convert-arith-to-llvm, -reconcile-unrealized-casts)
  -> llvm::Module              (mlir::translateModuleToLLVMIR)
```

**The handoff (the part that must be right):** per the interop rule in the tensor-lowering decision, MLIR produces a *separate* `llvm::Module` that is merged into the main one.

- `TensorEmitter` builds `func.func @z_tensor_kernel_<n>` with `memref` arguments, and returns the symbol name to `CodeGen`.
- `CodeGen` emits a normal `call` at the use site. Arguments are **unpacked memref descriptors** — MLIR's default calling convention passes `(allocated_ptr, aligned_ptr, offset, sizes..., strides...)` as separate arguments. Use `-llvm-request-c-wrappers` so MLIR also emits `_mlir_ciface_*` wrappers taking a single descriptor pointer; calling those from `CodeGen` is far less error-prone than replicating the unpacked ABI by hand.
- After the walk, `translateModuleToLLVMIR` produces the MLIR-side module and `llvm::Linker::linkModules` merges it into the main module. This must happen **before** the existing O2 `PassBuilder` run in `main.cpp`, so the optimizer sees one whole program and can inline across the boundary.
- Both modules must share one `llvm::LLVMContext` and agree on the data layout, or the link silently produces garbage.

**New CLI flags:** `--emit-mlir` (dump the MLIR module before lowering) and `--emit-mlir-llvm` (dump it after lowering, before merge). These are to M17a what `--emit-llvm` was to M0 — without them the pipeline is a black box.

**Test programs:**
- `Test/mlir/roundtrip_add.z` — a hardcoded 4-element `z.add`, verified numerically end-to-end
- `Test/mlir/emit_mlir.z` — `--emit-mlir` output contains a `z.add` op; `--emit-mlir-llvm` output contains no `z.` ops (proves the pipeline actually ran)
- `Test/mlir/link_smoke.z` — a program mixing an M2 `for` loop with one tensor op, proving the two IR paths coexist in one module

**Done when:** a Z program containing one elementwise add compiles to a working `.exe` whose tensor work went through MLIR, and every M0–M16 test still passes with `-DZ_ENABLE_MLIR=ON` *and* with it `OFF`.

### Milestone 17b: `tensor` Library — Core (CPU, no autograd, no GPU)

**Add:** the minimum viable `tensor` library — dense numeric tensors with dynamic or static shape, on CPU. No autograd (that's M19), no GPU (that's M20/M21), no fusion/static-shape tuning (that's M22). Imported via `using tensor`. Depends on the MLIR foundation (M17a), `structures` (M11) for `array<T>`, the full type system (M3), and overloading (M16). Does not depend on classes — the compiler special-cases tensor methods the same way M11 does for `structures`.

**Why split the monolithic tensor plan:** the original "tensor library" was really five projects in one slot (MLIR foundation + core + autograd + GPU + static-shape). Each is a multi-week effort on its own. Splitting them into M17a/M17b/M19/M20/M21/M22 makes each independently shippable, unblocks `regression` on just the core, and keeps the CUDA rabbit hole cleanly separable.

**Scope:** element types `float16`/`float32`/`float64`/`int32`/`int64`; dynamic-rank, dynamic-shape tensors on CPU; full broadcasting, reductions, and linear algebra. Enough to implement `regression` (M18) on top.

**Default dtype is `float32`.** `tensor<float>.randn([128, 64])` is the idiomatic form — matches PyTorch's default, has 2× the GPU throughput of `float64`, and is the standard for ML training. `float64` (`double`) is available when precision matters (regression, scientific computing). `float16` is available for inference and GPU workloads that need maximum throughput — but its reduced range (max ~65504) and precision make it unsuitable as a general default.

**Type syntax:**
```
tensor<T>                     # dynamic-rank, dynamic-shape tensor of T
tensor<T, d0, d1, ..., dN>    # pin the shape at compile time — enables static checks now, fusion and unrolling in M22
```

Static shapes are not just a compile-time check: they become *static dimensions in the MLIR type* (`tensor<2x2xf32>` rather than `tensor<?x?xf32>`), which is what lets M22 tile, vectorize, and unroll without any new codegen. Dynamic-shape tensors emit `tensor<?x?xf32>` and carry their dims as SSA values.

**Runtime representation (`Runtime/Headers/ztensor.h`, `Runtime/Main/ztensor.c`):**
The struct is defined with *all* fields that later milestones will need, so M19/M20 don't require ABI breaks — but the autograd and device fields are inert in M17b. MLIR does not see this struct: the runtime owns allocation, lifetime, and metadata, while MLIR-generated kernels operate on the raw buffer `data` points at, received as a `memref` descriptor. That split is what keeps the GC and the ABI out of the lowering pipeline.
```c
typedef struct ZTensor {
    ZGCHeader   gc_header;     // first field — makes ZTensor a GC-managed object like any class
    void*       data;          // CPU pointer in M17b; device pointer added in M20
    int64_t     rank;
    int64_t*    shape;         // length == rank
    int64_t*    strides;       // length == rank, in elements
    int32_t     dtype;         // Z_FLOAT16 | Z_FLOAT32 | Z_FLOAT64 | Z_INT32 | Z_INT64
    int32_t     device;        // always Z_CPU in M17b; Z_CUDA added in M20
    int32_t     requires_grad; // always 0 in M17b; honored in M19
    struct ZAutogradNode* grad_fn; // always NULL in M17b
    struct ZTensor*       grad;    // always NULL in M17b
} ZTensor;
```
Lifetime is handled by the tracing GC from M14 — tensors are ordinary GC-managed objects and do not need per-op retain/release. A `ZTensor_typeinfo` global tells the collector that `grad_fn` and `grad` are GC pointers; the `data` buffer and `shape`/`strides` arrays are allocated separately via `z_gc_alloc_bytes` and reached through the bitmap. No custom refcount, no special lifetime path — the tensor library benefits from the unified model the rest of the language already uses.

**Buffer ownership across the MLIR boundary.** MLIR's bufferization pass will happily allocate results itself, which would put tensor buffers outside the GC's knowledge. Avoid this: run bufferization with **destination-passing style** so every kernel writes into an output buffer the caller supplies. `CodeGen` allocates the result `ZTensor` (and its `data` buffer) through `z_gc_alloc_bytes` *before* the call, then passes it as the output memref. MLIR-generated code then never allocates or frees — it only reads and writes buffers the GC already owns. This is the single most important invariant in the whole tensor stack; getting it wrong means intermittent, unreproducible collector crashes.

**API in M17b:**
- **Construction:** `tensor<T>(nested_array_literal)`; factories `tensor<T>.zeros(shape)`, `.ones(shape)`, `.randn(shape)`, `.eye(n)`
- **Introspection:** `.shape() -> int[]`, `.rank() -> int`, `.size() -> int`, `.dtype() -> int`
- **Dtype casting (M17b):** `.to(float16)`, `.to(float)`, `.to(double)`, `.to(int32)`, `.to(int64)` — returns a new tensor with elements cast to the target type. `.half()` is a shorthand for `.to(float16)`; `.float()` for `.to(float)`; `.double()` for `.to(double)`. Mixed-dtype binary ops promote to the wider type following the same rules as scalar arithmetic.
- **Indexing / shape ops:** `.get(idx0, ...)`, `.set(idx0, ..., v)`, `.slice(dim, start, end)`, `.reshape(new_shape)`, `.transpose(d0, d1)`
- **Elementwise (broadcast-compatible):** `+`, `-`, `*`, `/` between tensors; scalar broadcast on either side
- **Reductions:** `.sum()`, `.sum(dim)`, `.mean()`, `.min()`, `.max()`, `.argmax(dim)`
- **Linear algebra:** `.matmul(other)`, `.dot(other)`, `.inverse()`, `.solve(b)` (Cholesky if SPD, LU otherwise), `.det()`, `.T` shorthand for transpose-of-last-two-dims
- **Activations (float16/float/double):** `.relu()`, `.sigmoid()`, `.tanh()`, `.exp()`, `.log()`

**Not in M17b** (deferred to named milestones):
- `.to(CUDA)` / `.to(CPU)` (device move) — M20. Note: `.to(dtype)` for dtype casting IS in M17b; `.to(device)` for GPU movement is M20.
- `.requires_grad(true)`, `.backward()`, `.grad()`, `.detach()`, `with no_grad() { }` — M19
- Fusion and static-shape tiling/vectorization — M22

**Implementation — what lowers to MLIR and what stays in C.** The split matters; getting it wrong means either a C runtime that MLIR was supposed to replace, or a doomed attempt to express LAPACK in `linalg`.

*Through MLIR (`linalg` on `tensor`):*
- **Elementwise ops** (`+`, `-`, `*`, `/`, and the activations `.relu()`, `.sigmoid()`, `.tanh()`, `.exp()`, `.log()`) — one `linalg.generic` each, differing only in the body region. This is where the op-count argument for MLIR pays off.
- **Broadcasting** — expressed as non-identity indexing maps on `linalg.generic` operands, not as a materialized broadcast. NumPy-style shape rules are computed in the compiler when emitting the maps (static shapes) or via `tensor.dim` + `arith` guards (dynamic shapes). A dedicated `z.broadcast_binop` op carries the intent until the `ZToLinalg` pass builds the maps.
- **Reductions** (`.sum()`, `.sum(dim)`, `.mean()`, `.min()`, `.max()`, `.argmax(dim)`) — `linalg.reduce`, or `linalg.generic` with a reduction iterator for `.argmax`.
- **`.matmul()` / `.dot()`** — `linalg.matmul` / `linalg.dot`. On CPU the default pipeline lowers these to loops; M22 tiles and vectorizes them.
- **Shape ops** — `.reshape()` → `tensor.reshape`, `.transpose()` → `linalg.transpose`, `.slice()` → `tensor.extract_slice`, `.get()`/`.set()` → `tensor.extract`/`tensor.insert`.

*Stays in the C runtime (`Runtime/Main/ztensor.c`):*
- **Allocation, `ZTensor` construction, shape/stride metadata, dtype tags, GC integration** — the runtime owns the object; MLIR only sees buffers.
- **Factories** `.zeros()`, `.ones()`, `.randn()`, `.eye()` — allocation plus a fill; not worth a lowering path. `.randn()` needs an RNG the pipeline has no business owning.
- **`.inverse()`, `.solve()`, `.det()`** — call into LAPACK (or a hand-written Cholesky/LU in C). `linalg` has no factorization ops and writing pivoting logic as `linalg.generic` is a bad trade. This is a documented, deliberate escape hatch — the same "the language can't express it yet" exception the stdlib-hosting decision allows.
- **Shape validation and the `z_tensor_abort` error path.**

*Compiler:*
- **Sema:** for `tensor<T, d0, ...>` forms, keep the compile-time shape and reject mismatches before codegen; emit static MLIR tensor types. For `tensor<T>`, emit `?` dims and defer shape checks to runtime guards.
- **Mangler:** `T<elemCode>` for `tensor<T>` (dynamic), `T<elemCode,d0,d1,...>` for static shape.
- **Kernel caching:** two occurrences of the same op at the same shape and dtype should emit one `func.func`, not two. Key the cache on `(op, operand types, static shape)`.

**Test programs:**
- `Test/tensor/construct.z` — dynamic and static construction; print `.shape()`, `.rank()`
- `Test/tensor/broadcast.z` — `(3,1) + (1,4) -> (3,4)` elementwise
- `Test/tensor/matmul.z` — 2×3 · 3×4 = 2×4; compare against hand-computed
- `Test/tensor/solve.z` — build a 3×3 SPD matrix, solve `A x = b`, verify `A·x ≈ b` (C/LAPACK path, not MLIR)
- `Test/tensor/shape_error_static.z` — compile-time shape mismatch → compile error
- `Test/tensor/shape_error_dynamic.z` — runtime shape mismatch → clear `z_tensor_abort` message
- `Test/tensor/no_import.z` — using `tensor<double>` without `using tensor` → expect error
- `Test/tensor/gc_stress.z` — allocate and drop tensors in a loop under a forced `z_gc_collect()`, verifying the destination-passing invariant holds and no MLIR-allocated buffer escapes the collector

### Milestone 18: `regression` Library

**Add:** the `regression` standard library — statistical regression models imported via `using regression`. Depends on `tensor` core (M17b) for compute, classes (M13), the GC (M14 — model objects need to be collectable), interfaces (M15), and overloading (M16).

**Regression is the acceptance test for M17b.** It is written entirely against the public tensor API and knows nothing about MLIR. If `regression` can be written without reaching past `tensor` into the runtime or the lowering pipeline, the M17b abstraction held.

**Architectural principle — the stdlib is self-hosted.** Regression model classes live in `stdlib/regression.z` as ordinary Z source code. When Sema sees `using regression`, it parses and type-checks that file the same way it parses user code and links the output in. The compiler does **not** synthesize `ClassDecl`s in C++ — doing so would hide the stdlib from users (can't grep/debug it), create two parallel systems (compiler-synthesized classes vs user classes), and skip the validation that the class system actually works for real code. The only legitimate compiler-special-casing is for types the language can't express (e.g. `list<T>` generics before real generics exist). `LinearRegression` is not in that category — it's a plain class with methods.

**Models provided:**
- `LinearRegression` — ordinary least squares (single or multiple predictors)
- `PolynomialRegression(degree: int)` — expands input to polynomial features then fits OLS
- `RidgeRegression(alpha: double)` — linear regression with L2 penalty; closed-form solution
- `LassoRegression(alpha: double)` — L1 penalty; coordinate-descent solver
- `ElasticNetRegression(alpha: double, l1_ratio: double)` — combined L1/L2 penalty
- `LogisticRegression` — binary classification via logistic link; gradient-descent solver
- `MultinomialLogisticRegression(num_classes: int)` — multi-class classification via softmax

**Common interface (`interface Regressor` in `stdlib/regression.z`):**
- `.fit(X: tensor<double>, y: tensor<double>) -> void` — trains on samples `X` (shape `[n_samples, n_features]`) and targets `y` (shape `[n_samples]`)
- `.predict(X: tensor<double>) -> tensor<double>` — point predictions on new samples
- `.score(X: tensor<double>, y: tensor<double>) -> double` — R² for regressors, accuracy for classifiers
- `.coef() -> tensor<double>` — fitted coefficients (raises if called before `.fit`)
- `.intercept() -> double` — fitted bias term
- `LogisticRegression` additionally has `.predict_proba(X) -> tensor<double>` returning class probabilities

**Implementation strategy:**
- Closed-form models (`LinearRegression`, `RidgeRegression`, `PolynomialRegression`) are written in pure Z on top of `tensor` — e.g. OLS is `coef = (X.T().matmul(X)).inverse().matmul(X.T()).matmul(y)`. No extra runtime code.
- Iterative solvers (`LassoRegression`, `LogisticRegression`, `ElasticNetRegression`, `MultinomialLogisticRegression`) land in pure Z in M18 using hand-written gradients (straightforward for these convex objectives). When M19 (autograd) lands, they can be rewritten to use `.backward()` without changing the public API — this is a nice validation of the autograd work, not a dependency.
- A small `runtime/zregression.c` exists only if numerical stability demands it (e.g. a robust L-BFGS for `LogisticRegression` if hand-rolled GD doesn't converge well enough for the tests). Prefer pure Z — add C only when needed.

**Preprocessing helpers (free functions in `stdlib/regression.z`):**
- `train_test_split(X, y, test_ratio: double) -> (X_train, X_test, y_train, y_test)`
- `standardize(X) -> (X_scaled, mean, std)`
- `r2_score(y_true, y_pred) -> double`, `mse(y_true, y_pred) -> double`, `mae(y_true, y_pred) -> double`
- `accuracy(y_true, y_pred) -> double`, `confusion_matrix(y_true, y_pred, num_classes) -> tensor<int64>`

**Sub-phases:**
- **M18a:** `LinearRegression` + `RidgeRegression` + metrics (`r2_score`, `mse`, `mae`). All pure Z on top of `tensor`.
- **M18b:** `PolynomialRegression` + `train_test_split` + `standardize`.
- **M18c:** `LassoRegression` + `ElasticNetRegression` (coordinate descent in pure Z).
- **M18d:** `LogisticRegression` + `MultinomialLogisticRegression` + `accuracy` / `confusion_matrix`. Hand-written gradients in Z; revisit with autograd once M19 lands.

**Sema rules specific to this library:**
- Reject `LinearRegression`, `fit`, etc. without `using regression` — normal import-gating from M4.
- `.fit(X, y)` requires `X.rank() == 2` and `y.rank() == 1` with matching first dimension. Static-shape tensors get compile-time checks; dynamic-shape tensors get runtime checks.
- Calling `.coef()` or `.predict()` before `.fit()` calls `abort()` with a clear message (per the error-handling decision from the Cross-Cutting Decisions section). Every model carries a `fitted: bool` field.

**Test programs:**
- `tests/regression/linear_simple.z` — `y = 2x + 1` synthetic data, verify `coef ≈ 2`, `intercept ≈ 1`, `R² > 0.99`
- `tests/regression/multiple.z` — multiple features, compare coefficients against a hand-computed reference
- `tests/regression/polynomial.z` — fit a cubic to noisy cubic data
- `tests/regression/ridge_vs_linear.z` — with correlated features, verify ridge shrinks coefficients vs OLS
- `tests/regression/lasso_sparsity.z` — verify some coefficients are driven exactly to zero
- `tests/regression/logistic_binary.z` — two Gaussian clusters, accuracy > 0.95
- `tests/regression/pipeline.z` — `standardize` → `train_test_split` → `LinearRegression` → `r2_score` end-to-end
- `tests/regression/no_import.z` — using `LinearRegression` without `using regression` → compile error
- `tests/regression/unfitted.z` — `.predict` before `.fit` → abort with clear message

### Milestone 19: `tensor` — Autograd (`.backward()`, `.grad()`, `with no_grad()`)

**Add:** tape-based reverse-mode autograd on top of M17b's tensor core. No changes to M17b or M18 public APIs — autograd is purely additive.

**Where the tape lives — runtime, not MLIR.** MLIR has `linalg`-level differentiation research and an `enzyme` ecosystem, and it is tempting to make `.backward()` a compiler transform. Don't, in v1. Z's autograd must be *dynamic*: the tape records the ops that actually executed, including through Z-level `if` and `while`, which the compiler cannot see through. So `ZAutogradNode` stays a runtime structure exactly as designed, and the backward pass calls the same MLIR-generated kernels as the forward pass — a gradient is just another elementwise/matmul op, already available from M17b. Each forward op registers a tape node naming the backward kernels to invoke; no new lowering path is introduced. A static `z.grad` transform over a captured graph is a plausible companion to M33's graph capture, and is explicitly post-v1.

**New API:**
- `.requires_grad(true) -> tensor<T>` — mark a leaf tensor as tracked (in-place flag set)
- `.backward()` — on a scalar output, walks the tape and accumulates `.grad` on every tracked leaf
- `.grad() -> tensor<T>` — the accumulated gradient, or a zero tensor if never populated
- `.detach() -> tensor<T>` — returns a view that breaks the tape
- `with no_grad() { ... }` — new AST block that suppresses tape recording inside it

**Implementation:**
- Activate the `requires_grad` / `grad_fn` / `grad` fields that were dormant in M17b.
- Each differentiable op records a `ZAutogradNode { op_kind, inputs[], saved_tensors[] }` when any input has `requires_grad=1`. The output's `grad_fn` points to that node.
- `.backward()` does a topological walk of the grad graph starting from the scalar output seed (`dL/dL = 1`) and dispatches to per-op backward kernels (e.g. `z_backward_matmul(saved_A, saved_B, upstream_grad) -> (grad_A, grad_B)`).
- Leaf `.grad` tensors accumulate (`+=`) — matches PyTorch so repeated `.backward()` without zeroing gives summed gradients.
- Backward kernels for: `+`, `-`, `*`, `/`, scalar broadcast, `matmul`, `sum`/`mean`, `relu`, `sigmoid`, `tanh`, `exp`, `log`, `transpose`, `reshape`.
- `no_grad` block: Parser emits `NoGradBlockStmt`. CodeGen sets a thread-local `z_autograd_enabled = 0` on entry and restores on exit.
- Memory: the tape holds `ZTensor*` as GC roots so intermediate tensors stay live across the forward pass. `.backward()` clears the tape when it returns; after that, unreferenced intermediates are reclaimed on the next collection. No `retain_graph` arg in v1.

**Regression benefit (optional follow-up):** `LogisticRegression` and the other iterative solvers can be simplified from hand-written gradients to `.backward()` — cleaner code, still produces identical numeric results. This is not required, just nice to have.

**Test programs:**
- `tests/tensor/autograd_linear.z` — `y = W.matmul(x) + b`; `loss = y.sum()`; `loss.backward()`; verify `W.grad()` matches expected
- `tests/tensor/autograd_mlp.z` — two-layer MLP with relu, single gradient step
- `tests/tensor/no_grad.z` — inference inside `with no_grad() { ... }`, verify tape is not built
- `tests/tensor/autograd_accum.z` — two `.backward()` calls accumulate into `.grad`

### Milestone 20: `tensor` — GPU Backend (NVIDIA CUDA)

**Add:** CUDA device support on top of M17b core and M19 autograd. The first GPU backend — establishes the `Device` abstraction layer that M21 (AMD ROCm/HIP) will reuse. Explicitly separable: if skipped, M0–M19 and M22 still form a complete CPU stack.

**Why CUDA first:** broader hardware availability for development (every NVIDIA consumer GPU works), more mature library stack (cuBLAS, cuDNN have been GA for over a decade), better community documentation, and the NVPTX toolchain is the best-tested target in MLIR's `gpu` pipeline. AMD's ROCm path (M21) reuses this milestone's lowering with a different target, validating that the abstraction is portable.

**This milestone is much smaller than it looks, because of M17a.** The ops were already written once as `linalg` in M17b. GPU support is a *second lowering of the same ops*, not a second implementation of them. Concretely, the pipeline forks after fusion:

```
linalg on memref
  -> tile to parallel loops        (-convert-linalg-to-parallel-loops)
  -> map to GPU                    (-gpu-map-parallel-loops, -convert-parallel-loops-to-gpu)
  -> outline kernels               (-gpu-kernel-outlining)   => gpu.module + gpu.func
  -> lower kernel body             (-convert-gpu-to-nvvm)
  -> serialize to cubin/PTX        (-gpu-module-to-binary)
  -> host side                     (-gpu-to-llvm) => cuModuleLoad / cuLaunchKernel calls
```

There are no `.cu` files to write for elementwise ops, reductions, or activations. What *is* hand-written in this milestone: the cuBLAS/cuDNN bindings for matmul and (later) conv, the device memory management, and the runtime glue.

**New API:**
- Device enum: `CPU = 0`, `CUDA = 1`, `ROCm = 2` (constant introduced here so M21 doesn't break the API)
- `.to(device: int) -> tensor<T>` — copies the tensor to the target device (no-op if already there)
- `.device() -> int` — returns the current device tag
- `tensor.cuda_device_count() -> int`, `tensor.cuda_set_device(idx: int)` — multi-GPU selection (single-process)
- No other API changes — every existing tensor op gains a CUDA code path internally.

**Implementation:**
- Activate the `device` field on `ZTensor`. `t.to(CUDA)` calls `cudaMalloc` + `cudaMemcpy`; `t.to(CPU)` is the reverse. The data pointer's interpretation depends on `device`. This is runtime C code, unchanged in character from M17b.
- **Device-abstraction layer (`Runtime/Headers/zdevice.h`):** all GPU ops go through a `ZDeviceOps` vtable indexed by `device`. M20 fills in the CUDA slot; M21 fills in the ROCm slot. The dispatcher is `dispatch_op(device, op_kind, args)`. Under MLIR this vtable dispatches *compiled kernel launches and library calls*, not forty hand-written implementations — but the indirection stays, because the runtime still needs one place to decide CPU-vs-device.
- **Generated kernels:** elementwise ops, broadcasting, reductions, activations, normalization, and their backward counterparts all come out of the `linalg` → `gpu` → `nvvm` pipeline above. The `z` dialect and `linalg` emission from M17b/M19 are unchanged — only the pass pipeline differs, selected by the target device.
- **Library-backed ops:** `matmul` links to cuBLAS (`sgemm`/`dgemm`/`hgemm` for fp16) rather than using a generated kernel — a generated `linalg.matmul` will not beat cuBLAS, and pretending otherwise wastes the milestone. Keep the generated path behind a flag for comparison; it is a genuinely instructive benchmark. cuDNN is linked and used for softmax and activations; conv waits for M28.
- **A hand-written `.cu` file is an escape hatch, not the pattern.** If a specific op lowers badly, write the kernel and document why — the same exception structure the stdlib-hosting decision uses. If this starts happening often, the `gpu`-dialect approach is failing and that is worth knowing early.
- Device dispatch: each op in the runtime checks `lhs->device == rhs->device`. Same device → that device's path via `ZDeviceOps`. Mixed device → explicit error asking the user to call `.to(...)`. **No implicit transfers** — matches PyTorch.
- Stream management: each device has a default stream; ops are enqueued, not synchronous. `.cpu()` (or `.to(CPU)`) implicitly synchronizes. Explicit `tensor.cuda_synchronize()` for benchmarking.
- Build: CMake detects CUDA via `find_package(CUDAToolkit)`. Opt-in with `-DZ_ENABLE_CUDA=ON`, which also requires `-DZ_ENABLE_MLIR=ON`. MLIR must have been built with the NVPTX target available — check `MLIR_ENABLE_CUDA_RUNNER` / the presence of `MLIRGPUToNVVMTransforms` in the installed package before starting, not halfway through. Without CUDA, `Z_CUDA` is an unsupported-device runtime error — the language still compiles and runs CPU-only.

**Regression benefit (automatic):** `LinearRegression.fit(X.to(CUDA), y.to(CUDA))` just works — the solver is written against tensor ops, so GPU tensors dispatch to GPU kernels with zero regression-library changes.

**Test programs:**
- `tests/tensor/cuda_roundtrip.z` — `.to(CUDA)`, matmul on device, `.to(CPU)`, compare against CPU result
- `tests/tensor/cuda_autograd.z` — gradient of GPU tensors matches CPU version within tolerance
- `tests/tensor/cuda_mixed_device.z` — mixed-device op produces a clear error
- `tests/tensor/cuda_multi_gpu.z` — `cuda_set_device(0)`, `cuda_set_device(1)`, verify tensors are placed on the selected device (skipped if only one GPU is available)
- `tests/regression/cuda_linear.z` — fit `LinearRegression` on GPU tensors, compare to CPU coefficients

### Milestone 21: `tensor` — GPU Backend (AMD ROCm/HIP)

**Add:** AMD GPU support via the HIP runtime and ROCm libraries (rocBLAS, MIOpen, RCCL). Plugs into the M20 `ZDeviceOps` dispatch layer — most of the work is filling in the ROCm slot of the existing vtable, not redesigning anything.

**Why split from M20:** the generated-kernel half of this milestone is close to free — swap `-convert-gpu-to-nvvm` for `-convert-gpu-to-rocdl` and target `amdgcn` in `-gpu-module-to-binary`. The rest is not:
- Different toolchain and runtime API (HIP vs CUDA driver), different kernel binary format (HSACO vs PTX/SASS), different driver init.
- Different libraries with different version cadences and feature gaps (rocBLAS lags cuBLAS slightly; MIOpen has narrower op coverage than cuDNN) — and these are exactly the hand-written, library-backed ops that MLIR does *not* generate for you.
- Different communication backend for distributed training (RCCL vs NCCL).
- Different supported hardware tiers — RDNA2/RDNA3/CDNA all have different feature support flags, and the target chip must be named explicitly at lowering time (`gfx1100`, `gfx90a`, …) rather than inferred.

Doing CUDA first proves the device-abstraction layer; doing ROCm second validates portability. If the abstraction needed even small changes to accommodate ROCm, those changes feed back into M20 — a sign it was leaky. The useful signal from this milestone is *how much* of it turns out to be a one-line target swap versus real work.

**New API:**
- Adds `ROCm = 2` device value (the constant was already reserved in M20). Everything else uses the same `.to(device)` / `.device()` API from M20.
- `tensor.rocm_device_count() -> int`, `tensor.rocm_set_device(idx: int)` — symmetric to the CUDA variants.

**Implementation:**
- **Generated kernels:** the M20 pipeline with `-convert-gpu-to-rocdl` in place of `-convert-gpu-to-nvvm`, and `-gpu-module-to-binary` targeting `amdgcn-amd-amdhsa` with an explicit `chip` (e.g. `gfx1100`). The `linalg` ops themselves are byte-identical to the CPU and CUDA paths — this is the payoff for the M17a decision and the main thing this milestone is testing.
- **Runtime glue:** `hipMalloc` / `hipMemcpy` / `hipModuleLaunchKernel` in the ROCm slot of `ZDeviceOps`, symmetric with M20's CUDA slot. No tensor op above the dispatch layer changes.
- **Library-backed ops:** rocBLAS for matmul, MIOpen for softmax/activations/conv. Where rocBLAS or MIOpen lacks an op, fall back to the *generated* kernel rather than a hand-written HIP one — with `linalg` in place, the generic path is a real fallback instead of a stub, which is a meaningful improvement over the hand-written-kernel plan.
- Build: CMake detects ROCm via `find_package(hip)` and `find_package(rocblas)`. Opt-in with `-DZ_ENABLE_ROCM=ON`, which also requires `-DZ_ENABLE_MLIR=ON` and an MLIR built with the AMDGPU target. CUDA and ROCm builds can be combined: a single `zc` binary can target both `CUDA` and `ROCm` device tags at runtime if both are detected.
- Stream / synchronize semantics match M20: per-device default stream, `.to(CPU)` implicitly synchronizes, `tensor.rocm_synchronize()` for explicit timing.

**Validation against M20:** every test from M20 that targeted CUDA gets a mirror test on ROCm. Numeric outputs should match within 1e-5 (fp32) / 1e-2 (fp16) — small differences come from cuBLAS vs rocBLAS using different fused-multiply-add ordering.

**Test programs:**
- `tests/tensor/rocm_roundtrip.z` — analog of `cuda_roundtrip.z` with `.to(ROCm)`
- `tests/tensor/rocm_autograd.z` — gradient on ROCm tensors matches CPU within tolerance
- `tests/tensor/rocm_vs_cuda.z` — same computation on both devices (skipped if either is unavailable); compare numerics
- `tests/tensor/rocm_multi_gpu.z` — multi-GPU placement (skipped on single-GPU machines)
- `tests/regression/rocm_linear.z` — fit `LinearRegression` on ROCm tensors

### Milestone 22: `tensor` — Fusion and Static-Shape Tuning

**Add:** performance work on the M17a pipeline. Pure performance — no API change, no semantic change, and (unlike the pre-MLIR version of this milestone) **no third implementation of every op**. This milestone is pass-pipeline configuration and measurement.

**Why this milestone shrank.** The original plan was "emit inline unrolled LLVM loop nests in CodeGen for compile-time-known shapes" — a from-scratch codegen path alongside the C runtime and the GPU kernels. Because M17b emits `linalg` with static dimensions in the type, the information that path needed is already in the IR, and the transformations are existing upstream passes. The work is choosing and ordering them, then proving the result is actually faster.

**Implementation:**
- **Fusion (the headline win):** `-linalg-fuse-elementwise-ops` collapses `x.matmul(W).relu()` into a single loop nest with no materialized intermediate. This lifts the "no operator fusion across tensor ops" ceiling listed at the end of this document, and it applies to dynamic shapes too — it is not gated on static shapes.
- **Tiling and vectorization on static shapes:** `-linalg-tile`, then `-convert-vector-to-llvm` after transforming to the `vector` dialect. Static dims let tile sizes divide evenly and let the unroller see trip counts, so small fixed-size ops (`tensor<double, 4, 4>` matmul) unroll completely.
- **Pipeline selection:** one pass pipeline for fully-static operands, one for dynamic. Same emission, different lowering — the branch is in `Pipeline.cpp`, not in `CodeGen.cpp`.
- **Benchmark hooks:** a `--mlir-pipeline=<default|fused|tiled>` flag so the same program can be compiled through each configuration and timed. Without this, "is it faster" is unanswerable and the milestone has no completion criterion.

**Test programs:**
- `Test/tensor/static_fast_path.z` — same computation with `tensor<double, 16, 16>` and `tensor<double>([...])`; verify identical results
- `Test/tensor/fusion_check.z` — `--emit-mlir-llvm` on `x.matmul(W).relu()` shows one loop nest and no intermediate buffer allocation
- `Test/tensor/static_bench.z` — micro-benchmark across `--mlir-pipeline` settings, confirming the tuned path is actually faster (smoke-level, not rigorous)

### Milestone 23: `nn` — Module System (`Module`, `Parameter`, `Buffer`)

**Add:** the `nn` library — the PyTorch `torch.nn` analogue. This milestone is the foundation every layer and model in M24–M30 builds on. Imported via `using nn`. Self-hosted in `stdlib/nn.z`.

**The four core abstractions:**

*`Parameter` — a learnable tensor:*
```z
class Parameter {
    let data: tensor<float>
    let grad: tensor<float>
    let requires_grad: bool

    Parameter(t: tensor<float>): data = t.requires_grad(true), grad = tensor<float>.zeros_like(t), requires_grad = true { }
}
```

*`Buffer` — a non-learnable tensor that travels with the module (e.g. running stats in `BatchNorm`):*
```z
class Buffer {
    let data: tensor<float>
    Buffer(t: tensor<float>): data = t { }
}
```

*`Module` — the base class for every layer and model:*
```z
class Module {
    let _parameters: map<string, Parameter>
    let _buffers:    map<string, Buffer>
    let _children:   map<string, Module>
    let _training:   bool

    Module(): _parameters = ..., _buffers = ..., _children = ..., _training = true { }

    virtual fn forward(x: tensor<float>) -> tensor<float> {
        # Must be overridden — abort on base-class call.
        abort("Module.forward not implemented")
    }

    fn parameters() -> array<Parameter>             # flatten this + every child
    fn named_parameters() -> array<(string, Parameter)>
    fn buffers() -> array<Buffer>
    fn children() -> array<Module>
    fn modules() -> array<Module>                    # depth-first walk

    fn train()             { self._training = true;  for c in self._children { c.train()   } }
    fn eval()              { self._training = false; for c in self._children { c.eval()    } }
    fn is_training() -> bool { return self._training }

    fn zero_grad() { for p in self.parameters() { p.grad = tensor<float>.zeros_like(p.data) } }

    fn to(device: int) -> Module                     # moves all tensors recursively
}
```

*`Sequential` — composition helper (full implementation lands in M24 with the layer set):*
```z
class Sequential : Module {
    let layers: array<Module>
    fn forward(x: tensor<float>) -> tensor<float> {
        let out: tensor<float> = x
        for layer in self.layers { out = layer.forward(out) }
        return out
    }
}
```

**Parameter / child registration semantics:**
- Assigning a `Parameter` to a field of a `Module` subclass automatically registers it under the field name. The compiler's class codegen emits a hidden registration call in the constructor for any field whose declared type is `Parameter`, `Buffer`, or `Module` (or subclass). This mirrors PyTorch's `__setattr__` magic.
- Implementation note: M23 adds a *Sema pass* that walks every `class C : Module` and emits the registration calls into the generated constructor. No syntax change for users — you just declare fields normally.

**Example:**
```z
using nn
using tensor

class TwoLayer : nn.Module {
    let fc1: nn.Linear   # not yet defined — comes in M24, but field declaration is M23
    let fc2: nn.Linear

    TwoLayer(in_dim: int, hidden: int, out_dim: int):
        fc1 = nn.Linear(in_dim, hidden),
        fc2 = nn.Linear(hidden, out_dim) { }

    fn forward(x: tensor<float>) override -> tensor<float> {
        return self.fc2.forward(self.fc1.forward(x))
    }
}
```

**Test programs:**
- `tests/nn/module_basics.z` — subclass `Module`, register parameters via field assignment, call `parameters()` and verify the names and count
- `tests/nn/train_eval.z` — `train()` / `eval()` flip propagates to children
- `tests/nn/zero_grad.z` — `zero_grad()` resets every parameter's gradient
- `tests/nn/to_device.z` — `module.to(CUDA)` moves every tensor recursively (skip if no GPU)

### Milestone 24: `nn` — Basic Layers, Activations, Normalization

**Add:** the smallest set of `nn` layers needed to build a standard feedforward network. Every layer is a `Module` subclass and integrates with M23 parameter tracking automatically. Self-hosted in `stdlib/nn.z` — most layers are 10–30 lines of Z code on top of the tensor library.

**Layers:**

*Linear:*
- `nn.Linear(in_features: int, out_features: int, bias: bool)` — `y = x @ W^T + b`. Default Kaiming-uniform init for `W`, zero for `b`.
- `nn.Bilinear(in1: int, in2: int, out: int)` — bilinear product.
- `nn.Identity()` — passthrough.

*Activations (all stateless `Module` subclasses; functional equivalents in `nn.functional`):*
- `nn.ReLU()`, `nn.LeakyReLU(negative_slope: double)`, `nn.PReLU()`, `nn.ELU(alpha: double)`, `nn.GELU()`
- `nn.Sigmoid()`, `nn.Tanh()`
- `nn.Softmax(dim: int)`, `nn.LogSoftmax(dim: int)`
- `nn.Softplus()`, `nn.SiLU()` (a.k.a. Swish), `nn.Mish()`

*Functional API (free functions, no module wrapper):*
- `nn.functional.relu(x)`, `nn.functional.gelu(x)`, `nn.functional.softmax(x, dim)`, `nn.functional.linear(x, W, b)`, etc. — every activation/Linear has a functional twin for use without a Module wrapper.

*Regularization:*
- `nn.Dropout(p: double)` — drops each element with probability `p` during `train()`, no-op in `eval()`.
- `nn.Dropout2d(p: double)` — channel-wise dropout (for conv outputs).

*Normalization:*
- `nn.BatchNorm1d(num_features: int, momentum: double, eps: double)` — running mean/var stored as `Buffer`s.
- `nn.BatchNorm2d(num_features: int, ...)` — same for 4D tensors.
- `nn.LayerNorm(normalized_shape: array<int>, eps: double)`
- `nn.GroupNorm(num_groups: int, num_channels: int, eps: double)`
- `nn.RMSNorm(dim: int, eps: double)` — used in modern LLMs.

*Initialization helpers (`nn.init` sub-namespace):*
- `nn.init.uniform(p: Parameter, a: double, b: double)`
- `nn.init.normal(p: Parameter, mean: double, std: double)`
- `nn.init.xavier_uniform(p: Parameter, gain: double)`, `nn.init.xavier_normal`
- `nn.init.kaiming_uniform(p: Parameter, mode: string, nonlinearity: string)`, `nn.init.kaiming_normal`
- `nn.init.zeros(p: Parameter)`, `nn.init.ones(p: Parameter)`, `nn.init.constant(p: Parameter, val: double)`

**`Sequential` — full implementation lands here:**
```z
using nn
let model: nn.Module = nn.Sequential([
    nn.Linear(784, 256),
    nn.ReLU(),
    nn.Dropout(0.2),
    nn.Linear(256, 64),
    nn.ReLU(),
    nn.Linear(64, 10)
])
```

**Test programs:**
- `tests/nn/linear_forward.z` — `Linear(3, 4)` on a `tensor<float, 2, 3>`, verify shape and gradient
- `tests/nn/relu_grad.z` — gradient of relu matches the reference
- `tests/nn/dropout_train_eval.z` — `Dropout(0.5)` zeros some elements in train mode, passes through in eval mode
- `tests/nn/batchnorm_running_stats.z` — running mean/var update across batches
- `tests/nn/sequential.z` — `Sequential` with three Linears + activations
- `tests/nn/init.z` — Kaiming and Xavier init produce expected variance

### Milestone 25: `nn.loss` — Loss Functions

**Add:** a complete suite of differentiable loss functions, each a `Module` subclass and (functionally) a free function in `nn.functional`. Self-hosted; every loss is a few lines on top of M19 autograd.

**Regression losses:**
- `nn.MSELoss(reduction: string)` — mean / sum / none. `reduction = "mean"` by default.
- `nn.L1Loss(reduction: string)` — mean absolute error.
- `nn.SmoothL1Loss(beta: double)` — Huber-style.
- `nn.HuberLoss(delta: double)`

**Classification losses:**
- `nn.CrossEntropyLoss(weight: tensor<float>, ignore_index: int, label_smoothing: double)` — combines `LogSoftmax` and `NLLLoss`. Numerically stable via log-sum-exp.
- `nn.NLLLoss(weight: tensor<float>, ignore_index: int)` — expects log-probabilities as input.
- `nn.BCELoss()` — binary cross-entropy on probabilities.
- `nn.BCEWithLogitsLoss(pos_weight: tensor<float>)` — fused sigmoid + BCE; numerically stable.
- `nn.FocalLoss(alpha: double, gamma: double)` — for imbalanced classification.

**Distribution / similarity losses:**
- `nn.KLDivLoss(reduction: string)` — KL divergence between distributions (input is log-probs).
- `nn.CosineEmbeddingLoss(margin: double)`
- `nn.TripletMarginLoss(margin: double)`
- `nn.MarginRankingLoss(margin: double)`

**Functional twins:** every loss has a functional version in `nn.functional` (e.g. `nn.functional.cross_entropy(input, target)`).

**Common interface:** every loss exposes `.forward(input, target) -> tensor<float>` and inherits `Module.parameters()` (no parameters; loss modules store config only — `weight`, `reduction`, etc.).

**Sema rules:**
- Loss modules with no learnable params still inherit `Module` for uniform usage in pipelines.
- `CrossEntropyLoss.forward(input, target)` requires `input.rank() == 2` (logits, [batch, num_classes]) and `target.rank() == 1` (class indices). Static-shape variants get compile-time checks; dynamic-shape variants get runtime checks.

**Test programs:**
- `tests/nn/loss_mse.z` — gradient of MSE matches `2*(pred - target)/n`
- `tests/nn/loss_cross_entropy.z` — verify against hand-computed log-softmax + NLL
- `tests/nn/loss_bce_logits.z` — fused BCEWithLogits matches sigmoid + BCE within tolerance
- `tests/nn/loss_kl.z` — KL of equal distributions is 0
- `tests/nn/loss_focal.z` — heavily imbalanced labels: focal loss > cross-entropy

### Milestone 26: `optim` — Optimizers and LR Schedulers

**Add:** the `optim` library — optimizers and learning-rate schedulers. Imported via `using optim`. Together with M23–M25 this is the milestone that unlocks **end-to-end training**: a user can write a complete training step with a model, loss, optimizer, data tensor, and gradient update.

**Optimizers (each a class that owns parameter references and per-parameter state):**

*Stochastic methods:*
- `optim.SGD(params: array<Parameter>, lr: double, momentum: double, dampening: double, weight_decay: double, nesterov: bool)`
- Accepts a flat `array<Parameter>` (typically obtained from `model.parameters()`).

*Adaptive methods:*
- `optim.Adam(params, lr, betas: array<double>, eps: double, weight_decay: double, amsgrad: bool)`
- `optim.AdamW(params, lr, betas, eps, weight_decay)` — decoupled weight decay.
- `optim.RMSprop(params, lr, alpha: double, eps: double, momentum: double, centered: bool)`
- `optim.Adagrad(params, lr, lr_decay: double, weight_decay: double, eps: double)`
- `optim.Adadelta(params, lr, rho: double, eps: double, weight_decay: double)`
- `optim.NAdam(params, lr, betas, eps, weight_decay, momentum_decay: double)`

*Common interface:* every optimizer exposes:
- `.step()` — updates every parameter using its current `.grad`.
- `.zero_grad()` — zeros every parameter's gradient (forwards to `Module.zero_grad()` semantics).
- `.state_dict() -> map<string, dynamic>` — for checkpointing (M31 reads this).
- `.load_state_dict(s: map<string, dynamic>)` — restore from checkpoint.

**Per-parameter options:** `optim.SGD(model.parameters(), lr=0.01)` is the common case; the per-group override (`[{"params": [...], "lr": 0.001}, {"params": [...], "lr": 0.01}]`) is supported but with `array<map>` rather than Python's literal-dict syntax.

**LR schedulers (in `optim.lr_scheduler` sub-namespace):**
- `optim.lr_scheduler.StepLR(opt, step_size: int, gamma: double)`
- `optim.lr_scheduler.MultiStepLR(opt, milestones: array<int>, gamma: double)`
- `optim.lr_scheduler.ExponentialLR(opt, gamma: double)`
- `optim.lr_scheduler.CosineAnnealingLR(opt, T_max: int, eta_min: double)`
- `optim.lr_scheduler.CosineAnnealingWarmRestarts(opt, T_0: int, T_mult: int, eta_min: double)`
- `optim.lr_scheduler.OneCycleLR(opt, max_lr: double, total_steps: int)`
- `optim.lr_scheduler.ReduceLROnPlateau(opt, factor: double, patience: int, threshold: double)` — needs `.step(metric)` form rather than `.step()`.
- All schedulers expose `.step()` (or `.step(metric)` for ReduceLROnPlateau) and `.get_last_lr() -> array<double>`.

**End-to-end training-loop example (the first time this is expressible):**
```z
using nn
using optim
using tensor

class MLP : nn.Module {
    let fc1: nn.Linear
    let fc2: nn.Linear
    MLP(): fc1 = nn.Linear(784, 128), fc2 = nn.Linear(128, 10) { }
    fn forward(x: tensor<float>) override -> tensor<float> {
        return self.fc2.forward(nn.functional.relu(self.fc1.forward(x)))
    }
}

fn main() -> int {
    let model: MLP = new MLP()
    let loss_fn: nn.CrossEntropyLoss = nn.CrossEntropyLoss()
    let opt: optim.Adam = optim.Adam(model.parameters(), 0.001)
    let sched: optim.lr_scheduler.StepLR = optim.lr_scheduler.StepLR(opt, 10, 0.9)

    for (epoch: int = 0; epoch < 50; epoch = epoch + 1) {
        let x: tensor<float> = tensor<float>.randn([32, 784])
        let y: tensor<float> = tensor<float>.randint(0, 10, [32])

        opt.zero_grad()
        let pred: tensor<float> = model.forward(x)
        let loss: tensor<float> = loss_fn.forward(pred, y)
        loss.backward()
        opt.step()
        sched.step()
    }
    return 0
}
```

**Test programs:**
- `tests/optim/sgd_step.z` — one SGD step matches hand-computed `p -= lr * grad`
- `tests/optim/adam_state.z` — Adam first/second moments updated correctly across two steps
- `tests/optim/lr_step.z` — `StepLR` halves learning rate every N steps
- `tests/optim/cosine.z` — `CosineAnnealingLR` traces the cosine curve
- `tests/optim/end_to_end.z` — train an MLP on a toy dataset, verify loss decreases monotonically over 100 steps

### Milestone 27: `data.loader` — Dataset, DataLoader, Samplers, Transforms

**Add:** the `Dataset` / `DataLoader` abstraction — extends the existing `data` library (M10). Imported via `using data.loader` (using the M5 dotted import). After M27, the training loop in M26 can be rewritten to iterate batches from a real data source instead of synthetic tensors.

**Core interfaces (abstract base classes):**

```z
class Dataset {
    virtual fn length() -> int { abort("not implemented") }
    virtual fn get(idx: int) -> dynamic { abort("not implemented") }
}

class IterableDataset {
    virtual fn iter() -> Iterator { abort("not implemented") }
}

class Sampler {
    virtual fn iter() -> Iterator { abort("not implemented") }
    virtual fn length() -> int    { abort("not implemented") }
}

class Iterator {
    virtual fn has_next() -> bool { abort("not implemented") }
    virtual fn next() -> dynamic  { abort("not implemented") }
}
```

**Concrete datasets:**
- `data.loader.TensorDataset(tensors: array<tensor<float>>) : Dataset` — wraps a tuple of tensors with a shared first dimension; `get(i)` returns `array<tensor<float>>` of slices.
- `data.loader.CSVDataset(path: string, target_col: string) : Dataset` — uses M10's CSV reader; returns `(features, target)` tuples.
- `data.loader.ImageFolder(root: string, transforms: array<Transform>) : Dataset` — directory-of-class-folders convention.

**Samplers (`data.loader.samplers`):**
- `samplers.SequentialSampler(dataset_size: int)` — 0, 1, 2, ..., n-1
- `samplers.RandomSampler(dataset_size: int, replacement: bool, num_samples: int)`
- `samplers.SubsetRandomSampler(indices: array<int>)`
- `samplers.WeightedRandomSampler(weights: tensor<float>, num_samples: int, replacement: bool)`
- `samplers.BatchSampler(base: Sampler, batch_size: int, drop_last: bool)`

**DataLoader:**
```z
class DataLoader {
    DataLoader(
        dataset: Dataset,
        batch_size: int,
        shuffle: bool,
        sampler: Sampler,             # nullable; if null, builds a default
        num_workers: int,             # 0 = single-threaded; > 0 deferred to post-M34
        drop_last: bool,
        collate_fn: CollateFn         # nullable; default stacks tensors
    )
    fn iter() -> Iterator             # yields a batch (array<tensor<float>>) per call
    fn length() -> int                # number of batches
}
```

**Transforms (`data.loader.transforms`) — composable preprocessing for image/text/numeric data:**
- `transforms.Compose(ts: array<Transform>)`
- `transforms.ToTensor()`, `transforms.Normalize(mean: array<double>, std: array<double>)`
- `transforms.Resize(size: array<int>)`, `transforms.CenterCrop(size: array<int>)`, `transforms.RandomCrop(size: array<int>, padding: int)`
- `transforms.RandomHorizontalFlip(p: double)`, `transforms.RandomRotation(degrees: double)`
- `transforms.ColorJitter(brightness: double, contrast: double, saturation: double, hue: double)`

**Threading:** v1 ships single-threaded only (`num_workers = 0`). Multi-process / multi-thread prefetch is deferred until after M34, since concurrency primitives are not in the language yet.

**Test programs:**
- `tests/data/tensor_dataset.z` — wrap two tensors, iterate with `batch_size=4`, verify shapes
- `tests/data/csv_loader.z` — read a CSV, use as `Dataset`, train one epoch
- `tests/data/sampler_random.z` — `RandomSampler` produces different orders across calls
- `tests/data/transforms.z` — `Compose([Resize, ToTensor, Normalize])` produces expected outputs
- `tests/data/training_with_loader.z` — training loop using `DataLoader` instead of synthetic tensors

### Milestone 28: `nn` — Convolutional Layers and Pooling

**Add:** convolutional and pooling layers. The first non-trivial layer family that requires GPU-side library coverage (cuDNN on M20, MIOpen on M21) for performance. Self-hosted Z layer wrappers around runtime kernels.

**Convolutions:**
- `nn.Conv1d(in_channels, out_channels, kernel_size, stride, padding, dilation, groups, bias, padding_mode)`
- `nn.Conv2d(...)` — same signature; the workhorse for image models.
- `nn.Conv3d(...)` — for video / volumetric data.
- `nn.ConvTranspose1d`, `nn.ConvTranspose2d`, `nn.ConvTranspose3d` — fractionally-strided / "deconv".

**Pooling:**
- `nn.MaxPool1d`, `nn.MaxPool2d`, `nn.MaxPool3d` (with `kernel_size`, `stride`, `padding`, `dilation`, `return_indices`)
- `nn.AvgPool1d`, `nn.AvgPool2d`, `nn.AvgPool3d`
- `nn.AdaptiveAvgPool1d/2d/3d`, `nn.AdaptiveMaxPool1d/2d/3d`
- `nn.MaxUnpool1d/2d/3d` (uses indices from the corresponding MaxPool)

**Padding modules:**
- `nn.ZeroPad2d`, `nn.ConstantPad2d`, `nn.ReflectionPad2d`, `nn.ReplicationPad2d`

**Implementation:**
- CPU: explicit loop nests with optional im2col for matmul-style convolution.
- CUDA (M20): `cudnnConvolutionForward` / `cudnnConvolutionBackwardData` / `cudnnConvolutionBackwardFilter` with workspace allocation. Algorithm selection via `cudnnGetConvolutionForwardAlgorithm_v7`.
- ROCm (M21): MIOpen analogues. Where MIOpen lacks a config, fall back to a hand-written HIP kernel using im2col + rocBLAS gemm.
- All conv backward kernels integrate with M19 autograd through the existing `ZAutogradNode` mechanism — `Conv2d.forward` records its tape entry just like every other op.

**Test programs:**
- `tests/nn/conv2d_forward.z` — `Conv2d(3, 16, 3)` on `tensor<float, 1, 3, 32, 32>`, verify output shape `[1, 16, 30, 30]`
- `tests/nn/conv2d_grad.z` — gradient matches a hand-computed reference on a tiny input
- `tests/nn/maxpool2d.z` — `MaxPool2d(2, 2)` halves spatial dims
- `tests/nn/lenet.z` — full LeNet-5 forward + backward + one optimizer step on synthetic data
- `tests/nn/cudnn_match.z` (requires GPU) — CPU and cuDNN/MIOpen produce identical outputs within tolerance

### Milestone 29: `nn` — Recurrent Layers (RNN, LSTM, GRU)

**Add:** recurrent layers for sequence data. Each can run on CPU, CUDA (cuDNN RNN routines), or ROCm.

**Layers:**
- `nn.RNN(input_size, hidden_size, num_layers, nonlinearity: string, bias, batch_first, dropout, bidirectional)`
- `nn.LSTM(input_size, hidden_size, num_layers, bias, batch_first, dropout, bidirectional, proj_size)`
- `nn.GRU(input_size, hidden_size, num_layers, bias, batch_first, dropout, bidirectional)`
- Single-cell variants for fine-grained loops: `nn.RNNCell`, `nn.LSTMCell`, `nn.GRUCell`

**API:**
```z
let rnn: nn.LSTM = nn.LSTM(input_size=128, hidden_size=256, num_layers=2)
# x: [seq_len, batch, input_size]
# h0: [num_layers * num_directions, batch, hidden_size]
let result: array<tensor<float>> = rnn.forward(x, h0, c0)   # returns [output, h_n, c_n]
```

**Packed sequences:**
- `nn.utils.rnn.pack_padded_sequence(input, lengths, batch_first, enforce_sorted)`
- `nn.utils.rnn.pad_packed_sequence(sequence, batch_first, padding_value, total_length)`
- `nn.utils.rnn.PackedSequence` — internal data type

**Implementation:**
- CPU: hand-written tight loops with unrolled cell ops.
- CUDA: `cudnnRNNForward` / `cudnnRNNBackwardData` / `cudnnRNNBackwardWeights` for stacked LSTM/GRU/RNN.
- ROCm: MIOpen RNN routines where available, fallback to per-step matmul for missing configs.

**Test programs:**
- `tests/nn/lstm_forward.z` — `LSTM(8, 16)` on `tensor<float, 5, 1, 8>`, verify output shape `[5, 1, 16]`
- `tests/nn/lstm_grad.z` — gradient matches CPU reference within tolerance
- `tests/nn/gru_bidir.z` — bidirectional GRU produces concatenated forward/backward output
- `tests/nn/packed_seq.z` — variable-length batch packed correctly

### Milestone 30: `nn` — Attention and Transformer

**Add:** attention layers and full transformer blocks. After M30 the language can express any modern LLM-style architecture. Self-hosted on top of M28 (linear layers handle Q/K/V projections) and M19 (autograd).

**Attention primitives:**
- `nn.functional.scaled_dot_product_attention(query, key, value, attn_mask, dropout_p, is_causal) -> tensor<float>`
  - Falls back to per-step matmul on CPU; uses fused flash-attention-style kernel on GPU when enabled.
- `nn.MultiheadAttention(embed_dim, num_heads, dropout, bias, add_bias_kv, add_zero_attn, kdim, vdim, batch_first)`

**Transformer blocks:**
- `nn.TransformerEncoderLayer(d_model, nhead, dim_feedforward, dropout, activation: string, norm_first: bool)`
- `nn.TransformerDecoderLayer(d_model, nhead, dim_feedforward, dropout, activation, norm_first)`
- `nn.TransformerEncoder(encoder_layer, num_layers, norm)`
- `nn.TransformerDecoder(decoder_layer, num_layers, norm)`
- `nn.Transformer(d_model, nhead, num_encoder_layers, num_decoder_layers, dim_feedforward, dropout, activation, custom_encoder, custom_decoder)`

**Embeddings (lives in `nn` not in attention but introduced here for transformer plumbing):**
- `nn.Embedding(num_embeddings, embedding_dim, padding_idx, max_norm, norm_type, scale_grad_by_freq, sparse)`
- `nn.EmbeddingBag` — sum/mean/max-pool over embedding lookups.

**Positional encodings (utilities, not Modules):**
- `nn.functional.positional_encoding_sinusoidal(seq_len, d_model) -> tensor<float>`
- `nn.functional.rotary_embedding(x, freqs) -> tensor<float>` — modern LLM standard (RoPE).

**Implementation:**
- Attention uses M19 autograd directly — no special-case backward needed for the unfused path.
- Fused flash-attention path (when `Z_ENABLE_FLASH_ATTENTION` is set at build time) is opt-in via `nn.functional.scaled_dot_product_attention`. On CUDA this links cuDNN's fused-attention routines; on ROCm this uses Composable Kernels' equivalent. On CPU it falls back to the unfused implementation.

**Test programs:**
- `tests/nn/attention_basic.z` — multi-head attention shape sanity, verify gradient matches manual unfused path
- `tests/nn/transformer_encoder.z` — single-layer encoder forward/backward
- `tests/nn/transformer_full.z` — small Transformer (2 layers, d_model=64) trains a copy-task to ~0 loss
- `tests/nn/embedding.z` — embedding lookup + gradient on indices
- `tests/nn/rope.z` — rotary positional embedding produces expected rotations

### Milestone 31: `nn` — Serialization and Checkpointing

**Add:** save/load for `Module`, `Parameter`, `Buffer`, and optimizer state. Imported as part of `nn` (no separate library). After M31, training can resume from disk; trained models can be shipped as files.

**Core API:**
- `module.state_dict() -> map<string, tensor<float>>` — flat dotted keys: `"fc1.weight"`, `"bn.running_mean"`, etc. Already present (per M23) but populated for the full nested module tree here.
- `module.load_state_dict(d: map<string, tensor<float>>, strict: bool)` — assigns by name; `strict = true` errors on mismatched keys.
- `nn.save(module: Module, path: string)` — writes a `.zpt` (Z PyTorch) file.
- `nn.load(path: string) -> Module` — reads a `.zpt` file. Class identity must be re-registered (similar to PyTorch's class registry); a runtime error fires if the saved class isn't known.
- `nn.save_state_dict(d: map<string, tensor<float>>, path: string)`, `nn.load_state_dict(path: string)` — state-dict only, agnostic to class definitions (the recommended portable form).

**File format `.zpt`:**
- Header: magic `ZPT0`, version, byte order
- Index: dotted parameter names + dtypes + shapes + offsets
- Body: tensor data in native float32/float16/etc.
- Optional metadata: free-form `map<string, string>` for user notes.
- Compatible across CUDA / ROCm / CPU — tensor data is always saved in CPU-side format and reloaded onto the requested device.

**Optimizer checkpointing:**
- `opt.state_dict()` and `opt.load_state_dict(d)` round-trip through the same format. Used to resume training mid-epoch.
- Schedulers participate the same way.

**Idiomatic checkpoint:**
```z
let ckpt: map<string, dynamic> = map<string, dynamic>()
ckpt.insert("model", model.state_dict())
ckpt.insert("optimizer", opt.state_dict())
ckpt.insert("epoch", epoch)
nn.save_dict(ckpt, "checkpoint_epoch_42.zpt")
```

**Test programs:**
- `tests/nn/save_load_state_dict.z` — train one step, save, load into a fresh model, verify outputs match
- `tests/nn/save_load_full_module.z` — `nn.save` then `nn.load`, verify class identity
- `tests/nn/checkpoint_resume.z` — interrupt training, resume from checkpoint, verify final loss matches uninterrupted run
- `tests/nn/strict_load.z` — `load_state_dict` with mismatched keys raises an error when `strict = true`

### Milestone 32: `amp` — Automatic Mixed Precision

**Add:** the `amp` library — automatic fp16/bf16 mixed-precision training. Imported via `using amp`. Cuts GPU memory roughly 2× and speeds up training 1.5–3× on tensor-core-equipped GPUs, with no API change for the trained models.

**Core API:**
- `amp.autocast(device: int, dtype: string)` — context block (using the M19 `with no_grad()` AST mechanism). Inside the block, eligible ops auto-cast their float inputs to the target dtype.
  - `dtype = "float16"` for NVIDIA GPUs (Volta+) or AMD CDNA.
  - `dtype = "bfloat16"` for Ampere+ NVIDIA, AMD MI210+, or modern CPUs.
- `amp.GradScaler()` — handles gradient scaling for fp16 (bfloat16 generally doesn't need scaling).
  - `.scale(loss: tensor<float>) -> tensor<float>` — multiplies by a dynamic scale factor.
  - `.step(opt: Optimizer)` — un-scales gradients and calls `opt.step()` if no inf/NaN was seen, else skips.
  - `.update()` — adjusts the scale factor for the next iteration.

**The op cast policy** (which ops auto-cast to fp16 vs stay in fp32):
- **Cast to fp16:** `matmul`, conv, RNN/LSTM/GRU, attention, `Linear` weight ops.
- **Stay in fp32:** softmax, log, exp, normalization (batchnorm, layernorm), loss reductions — anything numerically sensitive.
- The policy is encoded in a runtime table `z_amp_cast_policy[op_kind]`, making it auditable and tweakable per-build.

**Idiomatic AMP training step:**
```z
using amp

let scaler: amp.GradScaler = amp.GradScaler()

for batch in loader.iter() {
    opt.zero_grad()
    with amp.autocast(CUDA, "float16") {
        let pred: tensor<float> = model.forward(batch)
        let loss: tensor<float> = loss_fn.forward(pred, target)
    }
    scaler.scale(loss).backward()
    scaler.step(opt)
    scaler.update()
}
```

**Implementation:**
- The `autocast` block sets a thread-local `z_autocast_dtype`. Every cast-eligible op checks this on entry and inserts a temporary cast.
- bfloat16 — needs an `bfloat16` type extension to M3's primitive set, or stays as a runtime-only dtype (no scalar literals). v1 takes the runtime-only path: `bfloat16` is a `tensor` element dtype but not a scalar primitive.
- `GradScaler` reuses the autograd machinery — scaling is a normal `mul` op recorded on the tape.

**Test programs:**
- `tests/amp/autocast_matmul.z` — matmul inside `amp.autocast` produces fp16 result; outside produces fp32
- `tests/amp/grad_scaler.z` — scaler skips a step on inf, decreases scale, recovers
- `tests/amp/end_to_end_amp.z` — train an MLP under AMP, verify final loss matches non-AMP within tolerance

### Milestone 33: `jit` — Graph Capture, Scripting, ONNX-Style Export

**Add:** the `jit` library — turn a `Module` into a serializable, deployable computation graph that does not require the Z runtime. Imported via `using jit`.

**Two capture modes:**

*Tracing (`jit.trace`):*
- Records the forward pass on a sample input, captures the op sequence as a static graph.
- Loses Python-style control flow that depends on tensor values.
- Best for models with fixed structure.
- `jit.trace(module: Module, sample_input: tensor<float>) -> jit.ScriptModule`

*Scripting (`jit.script`):*
- Source-level analysis of the `forward` method to capture data-dependent control flow.
- Limited to a Z subset that maps cleanly to the graph IR (`if`, `for`, simple `while`, no closures).
- `jit.script(module: Module) -> jit.ScriptModule`

**`jit.ScriptModule`:**
- A `Module` subclass that owns a graph instead of a Z `forward` method.
- `.forward(x)` runs the graph through the Z tensor runtime — no source needed.
- `.save(path: string)`, `jit.load(path: string)` — `.zsm` (Z scripted module) file format with embedded graph IR + parameter tensors.
- `.graph() -> string` — human-readable IR dump for debugging.

**ONNX-style export:**
- `jit.export_onnx(module: ScriptModule, path: string, opset: int)` — writes the graph in ONNX format (ProtoBuf). Supported subset: linear, conv, pooling, RNN, attention, common activations, normalization. Unsupported ops emit a clear error listing them.
- Round-trip: a model exported to ONNX should run identically in any ONNX runtime (validation tests use ONNX Runtime if available on the build machine).

**Graph IR — reuse MLIR, don't invent a third IR.** The tensor-lowering decision pays off again here: an MLIR module in the `z`/`linalg` dialects *is* a typed DAG with shapes, dtypes, and a stable textual and binary serialization. Inventing a bespoke graph IR alongside it would mean maintaining two representations of the same thing and writing a converter between them.

- The captured graph is an `mlir::ModuleOp`. Tracing records ops into a module instead of into a bespoke node list; the shape and dtype information rides in the MLIR types already.
- `.save()` writes MLIR **bytecode** plus the parameter tensors in a `.zsm` container. `jit.load` parses the bytecode back — `mlir::parseSourceFile` and the bytecode writer do the work.
- `.graph()` is `module.print()`, i.e. free, and already human-readable in a format with existing tooling.
- `.forward(x)` on a loaded module runs the same lowering pipeline as M17b and calls the result. Deployment without the Z *compiler* is then a question of shipping the lowered artifact rather than a separate interpreter.
- ONNX export becomes an MLIR-to-ONNX translation over a constrained dialect subset (the `onnx-mlir` project's dialect is a reference point) rather than a walk over a hand-rolled graph.
- Unsupported constructs (custom Z classes, dynamic dispatch, `dynamic` values in tensor positions) cause `jit.script` / `jit.trace` to error with a precise diagnostic — the check is "does this lower into the supported dialect subset."

**Test programs:**
- `tests/jit/trace_mlp.z` — trace a 3-layer MLP, run the traced module, verify identical output
- `tests/jit/script_branch.z` — script a `forward` with a data-dependent if/else
- `tests/jit/save_load_scripted.z` — `.zsm` round-trip
- `tests/jit/export_onnx.z` — export a small model to ONNX, validate header and node count
- `tests/jit/unsupported_op.z` — `jit.script` on a module using `dynamic` in tensor position errors clearly

### Milestone 34: `distributed` — Data-Parallel Training

**Add:** the `distributed` library — multi-GPU and multi-node data-parallel training. Imported via `using distributed`. The final "real PyTorch" feature; combines M20/M21 GPU backends with M23–M27 training stack.

**Process group / initialization:**
- `distributed.init(backend: string, world_size: int, rank: int, init_method: string)` — backends: `"nccl"` (NVIDIA), `"rccl"` (AMD), `"gloo"` (CPU/cross-vendor).
- `distributed.world_size() -> int`
- `distributed.rank() -> int`
- `distributed.local_rank() -> int`
- `distributed.is_initialized() -> bool`
- `distributed.barrier()` — sync all processes
- `distributed.shutdown()`

**Collective communication primitives:**
- `distributed.all_reduce(t: tensor<float>, op: string)` — `op = "sum" | "mean" | "max" | "min" | "product"`. Modifies `t` in place.
- `distributed.all_gather(t: tensor<float>) -> array<tensor<float>>`
- `distributed.broadcast(t: tensor<float>, src: int)`
- `distributed.reduce(t: tensor<float>, dst: int, op: string)`
- `distributed.scatter(out: tensor<float>, ins: array<tensor<float>>, src: int)`
- `distributed.gather(t: tensor<float>, dst: int) -> array<tensor<float>>`
- `distributed.send(t: tensor<float>, dst: int)`, `distributed.recv(t: tensor<float>, src: int)`

**`DistributedDataParallel` wrapper:**
```z
class DistributedDataParallel : nn.Module {
    let module: nn.Module
    let device_ids: array<int>

    DistributedDataParallel(m: nn.Module, device_ids: array<int>):
        module = m, device_ids = device_ids { ... }

    fn forward(x: tensor<float>) override -> tensor<float> {
        return self.module.forward(x)
    }
    # Hooks: every parameter's backward gradient is all-reduced (mean) across ranks
    # automatically before the optimizer step.
}
```
- Each process owns a complete model replica on its assigned GPU.
- The wrapper installs autograd hooks that fire on each parameter's gradient post-backward, all-reducing before the optimizer step.
- Bucketed gradient reduction overlaps communication with backward computation.

**Distributed sampler:**
- `data.loader.samplers.DistributedSampler(dataset_size: int, num_replicas: int, rank: int, shuffle: bool, seed: int, drop_last: bool)` — returns disjoint index subsets per rank.

**Idiomatic distributed-training script:**
```z
using nn
using optim
using distributed
using data.loader

fn main() -> int {
    distributed.init("nccl", world_size=4, rank=getenv_int("RANK"), init_method="env://")
    let local_rank: int = distributed.local_rank()
    tensor.cuda_set_device(local_rank)

    let model: nn.Module = build_model().to(CUDA)
    let ddp: distributed.DistributedDataParallel = distributed.DistributedDataParallel(model, [local_rank])
    let opt: optim.Adam = optim.Adam(ddp.parameters(), 0.001)

    let sampler: data.loader.samplers.DistributedSampler = data.loader.samplers.DistributedSampler(
        dataset.length(), distributed.world_size(), distributed.rank(), true, 42, false)
    let loader: data.loader.DataLoader = data.loader.DataLoader(dataset, 32, false, sampler, 0, true, null)

    for (epoch: int = 0; epoch < 50; epoch = epoch + 1) {
        for batch in loader.iter() {
            opt.zero_grad()
            let loss: tensor<float> = loss_fn.forward(ddp.forward(batch.x), batch.y)
            loss.backward()
            opt.step()
        }
        distributed.barrier()
    }
    distributed.shutdown()
    return 0
}
```

**Backends:**
- **NCCL** for NVIDIA-only clusters — picks up automatically when M20 is built and `world_size > 1` on CUDA tensors.
- **RCCL** for AMD-only clusters — analogous; picks up from M21 build flags.
- **Gloo** for cross-vendor or CPU-only — slower but works anywhere.
- The backend selection is a string at `init()` time; mixing CUDA + ROCm in one job requires Gloo.

**Implementation:**
- All collectives lower to the corresponding `nccl*` / `rccl*` / `gloo` C calls.
- Bucket size for gradient reduction is configurable via `distributed.set_bucket_size_mb(n: int)` — default 25 MB matches PyTorch.
- Rendezvous is environment-variable driven (`MASTER_ADDR`, `MASTER_PORT`, `WORLD_SIZE`, `RANK`) — same convention as `torchrun`.

**Test programs:**
- `tests/distributed/init_singleton.z` — `world_size = 1` initialization works
- `tests/distributed/all_reduce_2gpu.z` — sum across two GPUs equals expected (skipped on single-GPU machines)
- `tests/distributed/ddp_training.z` — train across 2 GPUs, verify final loss matches single-GPU run within tolerance
- `tests/distributed/distributed_sampler.z` — `DistributedSampler` produces disjoint indices across ranks
- `tests/distributed/cross_vendor_gloo.z` — Gloo backend works on a CUDA + CPU mixed setup

---

## Key Implementation Details

### CMakeLists.txt

The M0–M16 shape (this is what is checked in and building today):
```cmake
cmake_minimum_required(VERSION 3.20)
project(ZCompiler LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(LLVM REQUIRED CONFIG)
add_executable(zc Src/main.cpp Src/Lexer.cpp Src/Parser.cpp Src/Sema.cpp Src/CodeGen.cpp)
target_include_directories(zc PRIVATE Include ${LLVM_INCLUDE_DIRS})
llvm_map_components_to_libnames(llvm_libs core support irreader codegen mc mcparser target x86info x86codegen x86desc x86asmparser passes)
target_link_libraries(zc PRIVATE ${llvm_libs})
```

From M3 the compiled program also links the C runtime, so add:
```cmake
add_library(zruntime STATIC
    Runtime/Main/zstring.c
    Runtime/Main/zdynamic.c)
target_include_directories(zruntime PUBLIC Runtime/Headers)
set_target_properties(zruntime PROPERTIES C_STANDARD 11)
```
`zruntime` is *not* linked into `zc` — it is linked into the programs `zc` produces. The driver's link step becomes `clang <obj> <path-to-libzruntime.a> -o <exe>`, so `main.cpp` needs to know that path (pass it as a compile definition from CMake rather than hardcoding it).

From M17a, MLIR is added behind an option (see M17a for the full pass and library list):
```cmake
option(Z_ENABLE_MLIR "Build the MLIR tensor backend" OFF)
if(Z_ENABLE_MLIR)
    find_package(MLIR REQUIRED CONFIG)
    list(APPEND CMAKE_MODULE_PATH "${MLIR_CMAKE_DIR}" "${LLVM_CMAKE_DIR}")
    include(AddMLIR)
    include(TableGen)
    add_subdirectory(Src/MLIR)
    target_link_libraries(zc PRIVATE ZMLIR)
    target_compile_definitions(zc PRIVATE Z_ENABLE_MLIR=1)
endif()
```

### `print` as a built-in
Recognize `print(expr)` in CodeGen as a special call to C `printf`. Format string chosen by expression type: `"%d\n"` int, `"%f\n"` float, `"%s\n"` string, `"%c\n"` char.

### Newline handling
Lexer emits TK_Newline tokens. Track bracket/brace/paren depth — suppress newlines inside balanced pairs. Parser consumes newlines as statement terminators.

### Variable allocation pattern
Always create `alloca` in the function entry block so LLVM's `mem2reg` pass can promote to SSA:
```cpp
AllocaInst* createEntryBlockAlloca(Function* fn, const string& name, Type* ty) {
    IRBuilder<> tmp(&fn->getEntryBlock(), fn->getEntryBlock().begin());
    return tmp.CreateAlloca(ty, nullptr, name);
}
```

### String literals
**Before M3:** `builder.CreateGlobalString("hello")` → returns a pointer to a NUL-terminated C string. This is what the compiler does today and it is only adequate while `print` is the sole string operation.

**From M3 onward this is wrong** and must be replaced. A Z `string` is a `ZString*`, not an `i8*` — it carries an explicit length and a GC header, and is not NUL-terminated. Emit each literal as a **global `ZString` constant**: a private global whose body is `{ ZGCHeader{immortal}, i64 length, [N x i8] bytes }`, with the literal expression evaluating to a pointer to it. Marking literals immortal in the header is what lets M14's collector skip them rather than trying to free static storage.

Do not carry an `i8*` representation alongside `ZString*` "just for `print`" — two representations of `string` is exactly the kind of divergence the stdlib-hosting decision warns about. `print` calls `z_string_cstr`, same as everything else.

### CodeGen must dispatch on Z types, not LLVM types
`print` currently picks its format string by inspecting the LLVM type of the generated value. This does not survive M3: `character` and `int32` are both `i32`, so a `character` prints as a number, and `string` and `dynamic` are both pointers, so neither can be told apart. The same collision hits `+` (integer add vs `z_string_concat`) and the comparison operators.

The fix is structural and should land early in M3: Sema already stores its conclusion in `Expr::resolvedType`, so CodeGen should read *that* and use the LLVM type only for the mechanical emission that follows. Any place in `CodeGen.cpp` that branches on `isIntegerTy(32)` / `isPointerTy()` to make a *semantic* decision is a latent bug.

### OOP — Memory layout (canonical)
Every class instance has exactly this layout. Both the GC and virtual dispatch agree on it:
```
struct C {
    ZGCHeader gc_header;   // always present — typeinfo + mark_flags + reserved (16 bytes)
    i8*       vtable_ptr;  // present only if C participates in polymorphism
    <B's fields in declaration order>   // parent prefix — identical in every subclass
    <C's own fields in declaration order>
}
```
Two different fields for two different jobs:
- `gc_header.typeinfo` — consumed by the collector to find GC-managed fields (pointer bitmap) and the destructor slot.
- `vtable_ptr` — consumed by virtual dispatch to find overridden methods. Present only for classes that have virtual methods or inherit from a class that does; non-polymorphic classes skip this 8-byte slot entirely.

**Upcast invariant:** a `C*` bitcast to `B*` works because the `[ZGCHeader, vtable_ptr, B fields]` prefix is identical between `C` and `B`. The child just appends more fields after.

Use `llvm::StructType::create(context, name)` first, then `setBody(...)` once layout is known (to support self-referential types).

### OOP — Vtable emission
For class `C` with virtual methods `m1, m2`:
```llvm
@C_vtable = constant [2 x i8*] [
    i8* bitcast (...to function pointer of C__m1...),
    i8* bitcast (...to function pointer of C__m2...)
]
```
Method-slot indices are assigned in inheritance order: parent slots first, child additions appended. Overridden slots keep the parent's index but point to the child's function.

### OOP — Virtual call codegen
```
; obj.draw()
%vtable = load i8**, i8** %obj.vtable_slot
%slot   = getelementptr i8*, i8** %vtable, i32 <drawIndex>
%fn     = load i8*, i8** %slot
%typed  = bitcast i8* %fn to void (C*)*
call void %typed(C* %obj)
```

### OOP — Name mangling scheme
- Free function: `name__<typecodes>` → `max(int,int)` = `max__i64i64`
- Method: `ClassName__name__<typecodes>` → `Circle.area()` = `Circle__area__v`
- Constructor: `ClassName__ctor__<typecodes>` → `Circle(double)` = `Circle__ctor__f64`
- Type codes (encode the width to keep overloads on different integer widths distinct):
  - `i32`, `i64`, `i128` — signed integers (`int` is platform alias, use `i64` on 64-bit)
  - `f16` float16, `f32` float (float32), `f64` double (float64)
  - `b` bool, `ch` character, `s` string, `v` void
  - `A<T>` array, `V<T>` vector, `L<T>` list, `Q<T>` queue, `S<T>` stack, `H<T>` heap, `B<T>` bstree, `M<K,V>` map, `U<K,V>` unordered_map
  - `T<elemCode,d0,d1,...>` tensor (fixed shape encoded in the mangling)
  - `P<ClassName>` class pointer
- Apply mangling in a single `ZMangler::mangle(name, paramTypes)` helper used by both decl emission and call site resolution

### OOP — Heap allocation (GC-managed)
Uses the canonical layout from the previous section: `[ZGCHeader, vtable_ptr?, parent fields, own fields]`. The `ZGCHeader` struct is:
```
struct ZGCHeader {
    i8*  typeinfo;     // per-class metadata: pointer bitmap, destructor slot (see below)
    i32  mark_flags;   // color bits used by the collector
    i32  reserved;     // padding / future use
}
```

Before M13 lands, `new ClassName(args)` calls `malloc` directly and the `ZGCHeader` is zero-initialized but ignored. After M13 lands, the same expression generates:
1. `%size = <sizeof ClassName struct>` (use `ConstantExpr::getSizeOf` or DataLayout)
2. `%raw = call i8* @z_gc_alloc(i64 %size, i8* @ClassName_typeinfo)` — allocates from the GC heap, zeroes the header, writes `typeinfo`
3. `%obj = bitcast i8* %raw to %ClassName*`
4. `call void @ClassName__ctor__<codes>(%ClassName* %obj, <args>)` — constructor sets `vtable_ptr` (if present) and all user fields
5. Use `%obj` as the result

`@ClassName_typeinfo` is a compiler-emitted global that tells the collector which offsets inside the object hold GC-managed pointers and whether the class has a destructor.

**Destructors as finalizers.** When the collector reclaims an object, it checks `typeinfo->destructor` and, if non-null, calls it before freeing the bytes. For a class with `~ClassName()` the compiler emits `ClassName__dtor` which runs the user's destructor body, then chains to the parent's destructor (`ParentClass__dtor`), then returns. Users never write the `super()` call — the compiler inserts it so the order is always child → parent. Finalizers are never called twice for the same object, never called on live objects, and never guaranteed to run promptly — code that needs deterministic resource release should not rely on them. See M13 for the full GC implementation and the restricted-body rules that keep finalizer behavior defined.

---

## Debug CLI Flags (implement in main.cpp)

- `--dump-tokens` — print token stream, one per line
- `--dump-ast` — pretty-print AST as indented tree
- `--emit-llvm` — print LLVM IR to stdout
- Default: compile to executable

These are essential for testing and debugging each phase independently.

---

## Testing Strategy

| Phase | Method |
|-------|--------|
| Lexer | `zc --dump-tokens test.z` → compare against expected token list |
| Parser | `zc --dump-ast test.z` → compare against expected AST dump |
| Sema | Deliberately broken `.z` files → check for expected error messages |
| CodeGen | Compile `.z` → run exe → compare stdout against `.expected` file |

Simple bash test runner:
```bash
for f in tests/codegen/*.z; do
    ./build/zc "$f" -o /tmp/test.exe && actual=$(/tmp/test.exe)
    expected=$(cat "${f%.z}.expected")
    [ "$actual" = "$expected" ] && echo "PASS: $f" || echo "FAIL: $f"
done
```

---

## Verification

After each milestone:
1. Build with `cmake --build build`
2. Run the corresponding example `.z` program
3. Check output matches expectations
4. Run `zc --emit-llvm` and inspect the IR for correctness
5. Run test suite for all previous milestones (regression testing)

## Critical Files
- `CMakeLists.txt` — must work first to verify LLVM linkage; grows the runtime library at M3 and the MLIR option at M17a
- `Include/Token.h` — foundational data type all phases depend on
- `Include/AST.h` — central data structure connecting parser → sema → codegen; holds `TypeRef`, which every milestone extends
- `Src/CodeGen.cpp` — most complex file, LLVM C++ API usage, where most learning happens
- `Src/main.cpp` — orchestrates the pipeline, implements debug flags, shells out to the linker
- `Src/MLIR/Pipeline.cpp` — from M17a, the second-most complex file: owns the lowering pass pipeline and the MLIR → LLVM module merge

---

## Ceilings and Escape Paths

Some choices in this plan are deliberate ceilings — they make v1 tractable but will need to be lifted if Z grows past a learning compiler. Naming them explicitly up front prevents them from being mistaken for permanent design:

- **No user-defined generics.** `list<T>`, `tensor<T>`, `regression` models work because the compiler special-cases them. A user cannot write `class Box<T> { ... }` in Z. This is the single largest ceiling — it locks every generic abstraction into the stdlib and forbids patterns like user-written `Result<T, E>`. Escape path: design a real generic system (monomorphization or type-erased) in a post-v1 milestone. The existence of `list<T>` as a compiler-blessed C-backed container is a bridge until then, not a template for new stdlib entries.
- **No user-defined libraries.** `using` only recognizes the four built-in names (`structures`, `math`, `tensor`, `regression`). There is no package manager, no module resolution, no file-path imports. Escape path: a future `using "path/to/file.z"` form, or a proper module system keyed on directory layout.
- **GC is mark-and-sweep, stop-the-world, non-moving.** v1 picks the simplest tracing collector that works. Pause times scale linearly with live-heap size and every allocation site can trigger a collection. Escape paths: generational (young-gen bump allocator + minor GC) for short-lived tensor intermediates; incremental or concurrent marking to cap pause times; a moving / compacting collector once the compiler can emit relocation-safe code. Any of these lifts the ceiling without changing the language surface.
- **Finalizers are best-effort.** Destructors run when the collector reclaims an object, with no timing guarantee and no ordering guarantee across unrelated objects. Code that needs deterministic cleanup (closing a file the instant it goes out of scope) has no ergonomic answer in v1. Escape path: add an explicit `using (resource) { ... }` scope form or `defer` statement later.
- **Tensor GPU support is NVIDIA-first, AMD second.** M20 lands CUDA, M21 lands ROCm. Escape path for other vendors: because kernels are generated through MLIR's `gpu` dialect, a third backend is mostly a new lowering target — `spirv` for Vulkan, or Metal via SPIR-V translation — rather than a third hand-written kernel library. Not v1 work, but materially cheaper than it would have been under the C-runtime plan.
- **MLIR is a heavy, version-locked dependency.** From M17 onward the compiler needs a matching LLVM+MLIR pair (~1.2 GB installed), and an LLVM upgrade becomes a coordinated bump of both. Escape path: none, really — this is the price of the tensor-lowering decision. It is mitigated by `-DZ_ENABLE_MLIR=OFF` keeping M0–M16 buildable on a plain LLVM install, so only people working on the tensor stack pay it.
- **Two IRs in one compiler.** Scalars and control flow go through `CodeGen.cpp` to LLVM IR directly; tensor expressions go through MLIR and are linked back in. The boundary is a real cost: kernels must be outlined into functions, values must cross as memref descriptors, and optimizations cannot see through the boundary until after the module merge. Escape path: migrate the whole compiler to MLIR-as-primary-IR (`func`/`scf`/`arith` for everything) post-v1, at which point the boundary disappears. That is a rewrite of every codegen milestone and is deliberately not attempted here.
- **`float16` scalar arithmetic is software-emulated on CPUs without AVX-512 FP16.** `float16` as a language scalar type is rare in practice — its value is as a tensor storage dtype where the runtime loops handle the precision. If someone writes `let x: float16 = 3.14f16 * 2.0f16` on a CPU without native FP16 support, LLVM will emit a libcall. This is correct but slow. Escape path: add a Sema warning when `float16` is used outside a `tensor<float16>` context on a non-GPU target; or restrict `float16` to tensor element types only. For v1, allow it and let LLVM handle the lowering.
- **~~No operator fusion across tensor ops.~~ Lifted by the MLIR decision, realized in M22.** This was a ceiling under the C-runtime plan, where each op was an opaque call and `x.matmul(W).relu()` necessarily materialized an intermediate. With ops emitted as `linalg`, `-linalg-fuse-elementwise-ops` handles this as a standard pass, for dynamic shapes as well as static. It remains a ceiling only in the sense that it is unimplemented until M22, not that it is architecturally out of reach. Retained here as a record of why the tensor-lowering decision was made.
- **No concurrency model.** No threads, no async, no channels. Every program is single-threaded. Escape path: pick a model (threads + mutexes, async/await, actors) only when a concrete use case demands it — speculating now would bake in the wrong primitives.
- **Error handling is `abort()` in v1.** Recoverable errors have no ergonomic path. Escape path: add `Result<T, E>` once real generics land — both ceilings lift together.

When a milestone hits one of these ceilings, the right move is usually to document it and work around it — not to lift the ceiling mid-milestone. Lifting is its own project.
