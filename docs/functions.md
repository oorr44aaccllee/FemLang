# Functions and closures

Functions are the user-defined callable unit: `fn name(params):` declares one,
and calling it evaluates its body in a fresh local environment.

```femlang
fn add(a, b):
    return a + b

print(add(2, 3))        # 5
print(add(20, 22))      # 42
```

## Syntax

A definition binds `name` to a function value (like a `let`, and therefore
immutable: `f = ...` is a runtime error).

```
fn name(param, other, ...):
    body statements
```

- `fn`, the name, and `(` are required; the list may be empty.
- Parameters are identifiers; they are **immutable** bindings local to the call
  (`x = 2` inside the body is a runtime error).
- The body is an indented block. A definition is a statement: it evaluates to
  null as an expression statement, and the program's final value is the last
  expression statement, exactly as with `let`.

A definition may legally appear anywhere a statement can, including inside
another function's body (that is what makes closures and higher-order functions
work).

## Calling

`name(a, b)` works for user functions exactly like native calls (`print`, ...):

- Every argument is evaluated fully before the call.
- The number of arguments must match the parameter count:
  `Runtime error: function 'twice' expects 2 arguments, got 1`.
- The callee may be any expression yielding a function value (a name, a
  parameter, a value returned by another function). Anything else reports
  `attempt to call a non-function value`.
- User functions can call natives and vice versa; a native error surfaced
  inside a user function stops the program with the native's message.

## Return values

- `return expr` aborts the body immediately and yields `expr` to the caller.
- A bare `return` (or falling off the end of the body) yields `null`.
- `return` outside any function is a runtime error:
  `return outside a function`.

## Local scope

Each call gets a **frame**: a new environment parented to the environment the
function was defined in (its closure). Lookup walks the enclosing chain, so a
function sees the bindings of every scope it was defined inside, and locals,
parameters, and reuse of an outer name inside the body never leak into the
caller.

```femlang
let x = 100
fn f(x):
    let y = 7
    return x + y
f(1)        # 8; the global x is untouched
```

Re-declaring a name with `let` rebinds the shared variable slot (capture by
variable, like Python), which is why a closure observes later rebinding in its
defining scope.

## Recursion

Functions may call themselves and each other (mutual recursion), because a
function binding is available inside its own body.

```femlang
fn fib(n):
    if n < 2:
        return n
    return fib(n - 1) + fib(n - 2)

fib(10)     # 55
```

The evaluator is a tree walker that recurses through the C stack, so runaway
recursion is capped at 256 active calls. Beyond that the program stops with
`recursion limit exceeded` instead of crashing the host process. Deep-but-legal
recursion (say 200 frames) fits comfortably under the cap.

## Closures

A function remembers where it was defined, not where it is called. That makes
higher-order and escaping closures work:

```femlang
fn make_counter(start):
    mut count = start
    fn step():
        count = count + 1
        return count
    return step

let a = make_counter(10)
let b = make_counter(100)
a()     # 11   (a and b own separate 'count' slots)
b()     # 101
```

Because frames are reference-counted, an escaping closure keeps its captured
environment alive as long as the closure itself is reachable. `mut` variables
captured by a closure are shared: a closure can read and update its captured
variables, and later calls observe those updates.

### Ownership notes

- Environments are reference-counted (see `docs/ownership.md`). A function
  value retains its closure; each call frame retains its parent.
- Storing a function into the very environment it closes over (the idiomatic
  self-recursive case) is a **weak binding**: no retain is taken, which breaks
  the reference cycle and lets the environment be freed when its last external
  reference drops.
- Capturing mutable state whose closure outlives the caller is fine. The one
  documented limitation: a cycle formed across *two different frames* (A
  captures a reference to B while B captures a reference to A, where neither
  frame is a plain self/binding cycle the weak rule covers) would not be
  collected. This cannot be expressed with today's value model and is noted in
  `docs/ownership.md`; it is deferred rather than treated with GC.

## Functions as values

- `str(f)` prints `<fn 'name'>`.
- `type(f)` is `"fn"`.
- `print(f)` prints `<fn 'name'>`.
- Functions never compare equal with `==` (like native functions): `f == f`,
  `f == g`, and `f == 1` are all `false`, so `f != g` is always `true`.

## Example

See `examples/functions.fem` for a runnable tour: arithmetic functions,
recursion, closures, counters, composition, and higher-order use.