# Standard library

The standard library is a set of built-in native functions registered at
startup into the environment's native registry by `fem_stdlib_register()`
(`src/builtins.c`, `include/builtins.h`). The CLI registers the full library in
`main()` (`src/eval_main.c`) right after creating the environment, so every
program can call them without imports; embedders can opt out by simply not
calling `fem_stdlib_register()`.

Like any native, the builtins receive already-evaluated argument values, must
not retain the borrowed argument array, and report problems by returning a
`VALUE_ERROR` value (see `docs/ownership.md`). The evaluator turns that into
the environment error channel and stops the program.

## `print(a, b, ...)`

Prints the arguments separated by single spaces and ends with a newline.
Strings are printed as raw bytes (no quotes); every other value uses its
display form. Returns `null`.

```femlang
print("hi", 42, true, 3.5)   # hi 42 true 3.5
```

## `len(s)`

Length of a string (byte count) or a list (element count). Expects exactly one
string or list argument; anything else is an error (`len() expects a string or
list`).

```femlang
len("hello")     # 5
len([1, 2, 3])   # 3
len([])          # 0
```

## `int(x)`

Truncates a float toward zero; converts a bool to `1`/`0`; passes integers
through unchanged. Expects exactly one argument of a numeric or bool type;
anything else (strings, `null`, natives) is an error. Floats outside the
`int64` range are an error (`int() argument out of range`).

```femlang
int(3.9)    # 3
int(-3.9)   # -3
int(true)   # 1
```

## `float(x)`

Converts an integer or bool to a float; passes floats through unchanged.
Expects exactly one argument of a numeric or bool type.

```femlang
float(7)      # 7.0
float(false)  # 0.0
```

## `str(x)`

Display form of any value as a string. Strings are returned as raw bytes
(no quotes).

```femlang
str(42)     # "42"
str(true)   # "true"
str(null)   # "null"
str("x")    # "x"
```

## `type(x)`

Name of a value's dynamic type: `"null"`, `"bool"`, `"int"`, `"float"`,
`"string"`, `"list"`, `"native"`, `"fn"`, or `"error"`.

```femlang
type(1)      # "int"
type([1, 2]) # "list"
type(print)  # "native"
type(add)    # "fn"   (a user-defined function)
```

## Example

See `examples/stdlib.fem` for a runnable tour of the whole library.