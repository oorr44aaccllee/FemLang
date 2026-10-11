# Playful aliases

FemLang keeps a conventional core and offers a layer of optional, playful
spellings for the same constructs. Every alias is a pure synonym: a program
that uses aliases behaves exactly like the equivalent program written with the
canonical names.

## Keyword aliases

These are handled by the lexer (`src/lexer.c`): the alias word produces the
same token as its canonical keyword.

| Alias | Canonical | Meaning |
| --- | --- | --- |
| `spark` | `let` | immutable binding |
| `serve` | `return` | return from a function |
| `slay` | `break` | leave a loop |
| `skip` | `continue` | next iteration |
| `uwu` | `fn` | function definition |
| `owo` | `if` | conditional |
| `nya` | `else` | conditional fallback |
| `rawr` | `while` | condition loop |

```femlang
uwu sign(n):
    owo n > 0:
        serve 1
    nya:
        serve 0

mut countdown = 3
rawr countdown > 0:
    print(countdown)
    countdown = countdown - 1
```

## `meow` (native alias for `print`)

`print` is an ordinary native function, not a keyword, so its alias is
registered in the standard library (`src/builtins.c`) rather than in the
lexer. `meow` and `print` are the same native callback:

```femlang
meow("hello", 1, [2, 3])   # same as print(...)
```

`meow` is an identifier like any other: it can be stored, passed around, and
inspected (`type(meow)` is `"native"`).

## Scope

- Aliases only cover the words listed above; every other identifier is
  unaffected, and aliases are case-sensitive (`Spark` is a normal identifier).
- The conventional spellings always exist and are never removed; the aliases
  are sugar for people who like them and can be ignored entirely.
