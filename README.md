# FemLang

FemLang is a small, expressive, Python-inspired programming language designed to be easy to learn, pleasant to use, and practical for real-world work. The goal is to blend a clean, intuitive syntax with a fast, portable runtime written in pure C11.

The project is intentionally inspired by the creativity, self-expression, and community spirit often associated with femboy culture, while keeping the language itself professional and broadly usable. The playful vocabulary is optional and should feel like a welcoming layer on top of a conventional, production-ready language core.

FemLang is best understood as a language with a strong identity, but not at the cost of clarity or engineering discipline.

## Core design philosophy

FemLang is designed around a few principles:

- simple and readable syntax
- safe, explicit behavior
- fast execution with a lightweight runtime
- easy interoperability with native C libraries
- optional playful aliases without compromising professional use
- a balance between aesthetic identity and technical seriousness

## Minimal first release

The first release is intentionally small but useful.

The minimal viable language should support:

- integers
- strings
- booleans
- lists
- functions
- conditionals
- loops
- native C function binding

This is enough to validate the core language model without prematurely committing to a large VM or a broad standard library.

The playful vocabulary remains optional:

```femlang
spark name = "Alex"
serve "Hello, " + name + "!"
```

The equivalent conventional version is:

```femlang
let name = "Alex"
return "Hello, " + name + "!"
```

This keeps the language welcoming without making it feel gimmicky or less suitable for serious software work.

## Example syntax

```femlang
let name = "femboy"
let age = 21
let cute = true

fn greet(person):
    if cute:
        return "Hello, " + person + " :3"
    else:
        return "Hello, " + person

let message = greet(name)
print(message)
```

## Planned architecture

The project is organized around a conventional compiler/runtime pipeline:

- Lexer
- Parser
- AST
- Bytecode compiler (planned)
- VM (planned)
- Native C integration
- runtime memory management

The present milestone implements the lexer, AST, parser, tree-walking
evaluator, environment model, native call expressions, and the native
standard library; bytecode, VM, closures, functions, and lists remain planned.

## Project status

Current milestone:

- tokenizer foundation complete with indentation handling
- C11 lexer, AST, and parser implemented
- tree-walking evaluator with immutable (`let`) and mutable (`mut`) bindings
- safe variable reassignment (deep-copied values, error channel)
- ownership-safe native C function registry (see docs/ownership.md)
- call expressions wired to the native registry, including `print`
- comparison operators (`< <= > >=`), float remainder, and type-safe errors
- native standard library (`print`, `len`, `int`, `float`, `str`, `type`),
  with `VALUE_ERROR` reporting (see docs/stdlib.md)
- command-line interpreter: `./femlang examples/basic.fem`
- automated regression test suite: `make test`

Planned next milestones:

- function declarations
- loops and lists
- bytecode compiler and VM

## Build and run

```bash
make
./femlang examples/basic.fem    # prints 30
make test                       # runs the regression suite
make clean                      # remove build artifacts
```

`make asan` builds with `-fsanitize=address,undefined`; the w64devkit toolchain
used on this machine does not ship the sanitizer runtimes, so build the normal
targets there (see docs/baseline-validation.md).

## Repository goals

FemLang is meant to be:

- approachable for newcomers
- expressive for small scripts and prototypes
- performant enough for real workloads
- friendly to C integration
- a creative but technically grounded language project

## License

MIT

## Notes

This repository is intentionally a foundational scaffold. The aim is to establish a sane design and a clean starting point before expanding into a larger language runtime.

The tone is friendly and expressive, but the implementation remains rooted in professional engineering practices.

---

FemLang is the kind of project that should feel fun without sacrificing correctness, clarity, or maintainability.
