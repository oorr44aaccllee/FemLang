# Safe variable reassignment

FemLang now distinguishes immutable and mutable bindings:

```femlang
let fixed = 1
mut score = 0
score = score + 1
score
```

Assigning to `fixed` is rejected by the environment API. The evaluator uses
value copies for stored strings, so replacing a mutable string releases the
old value without sharing ownership with the expression result.

The current build targets are:

```bash
make
make test
make asan
```

The next phase is native C functions, followed by FemLang function declarations
and calls.
