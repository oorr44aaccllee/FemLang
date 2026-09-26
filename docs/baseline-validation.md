# Baseline validation and native bindings

This milestone adds baseline regression checks for immutable and mutable
variables and introduces a small, ownership-safe C native function registry.

## Reassignment rules

```femlang
let fixed = 1
fixed = 2       # runtime error: cannot reassign immutable variable
                # (rejected by the environment API)

mut score = 1
score = score + 2
```

`let` bindings are immutable. `mut` bindings can be replaced. Values stored in
the environment are deep-copied, so strings never share ownership with
temporary expression results. See docs/ownership.md for the full ownership model
and docs/reassignment.md for the statement-level rules.

## Native registry

The first native-C layer is intentionally independent of the parser. It
provides a small registry that maps names to C callbacks:

```c
Value native_add(size_t argc, const Value *argv);

FemNativeRegistry registry;
native_registry_init(&registry);
native_register(&registry, "add", native_add);
```

Behavior:

- `native_register()` copies the name and can be called repeatedly; registering
  an existing name replaces the function and keeps the name copy.
- `native_lookup()` returns the stored callback, or NULL for unknown names.
- The registry grows by reallocation (initial capacity 8) without losing
  existing entries.
- NULL registry / name / function are rejected.
- `native_registry_free()` is idempotent.

The evaluator consumes this registry for call expressions: an identifier that
is not a user binding but is registered in the environment's registry resolves
to a `VALUE_NATIVE`, and `f(a, b)` invokes the callback with freshly evaluated
argument values. Registration stays environment-scoped (`env_native_registry`);
keep it separate from the parser so the registry remains testable in isolation.

## Validation commands

```bash
make clean
make
make test
```

This is what the current machine produces:

```text
All FemLang tests passed.
```

The repository Makefile also defines `make asan`, which passes
`-fsanitize=address,undefined` at both compile and link time. Note: the toolchain
used here (w64devkit w64-w64-mingw32 GCC) does not ship `libasan` or `libubsan`,
so the sanitizer link step fails with `cannot find -lasan` on this machine. On a
toolchain that provides the sanitizer runtimes (Linux/macOS GCC or Clang), the
same target builds and runs the test suite under the sanitizers. As a substitute
on this machine, the sources compile clean under `-fanalyzer` with no findings.

## Scope boundaries

Not yet implemented: user-defined functions (`fn`), list literals, loops, and
`elif`. Call expressions, native invocation from FemLang source (`print`),
comparison operators `< <= > >=` in the evaluator, and the native standard
library (`print`, `len`, `int`, `float`, `str`, `type` — see
`examples/stdlib.fem` and `docs/stdlib.md`) are implemented since this and the
following milestones. Those are the next milestones.