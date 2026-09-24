# Baseline validation and native bindings

This milestone adds baseline regression checks for immutable and mutable variables and introduces a small, ownership-safe C native function registry.

## Reassignment rules

```femlang
let fixed = 1
fixed = 2       # rejected by the evaluator

mut score = 1
score = score + 2
```

`let` bindings are immutable. `mut` bindings can be replaced. Values stored in the environment are copied, so strings do not share ownership with temporary expression results.

## Native registry

The first native-C layer is intentionally independent of the parser. It provides a small registry that maps names to C callbacks:

```c
Value native_add(size_t argc, const Value *argv);

FemNativeRegistry registry;
native_registry_init(&registry);
native_register(&registry, "add", native_add);
```

The evaluator will consume this registry once call-expression parsing is enabled. Keeping registration separate makes the current baseline easier to test and avoids coupling C callbacks to unfinished function syntax.

## Validation commands

```bash
make clean
make
make test
make clean
make asan
```

The repository tools available to this task do not execute shell commands, so the commands above must be run in a local checkout or CI. This commit updates the Makefile so sanitizer flags are passed at link time as well as compile time.
