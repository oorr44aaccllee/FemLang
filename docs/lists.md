# Lists

FemLang has ordered, heterogeneous lists. A list can hold any value,
including other lists, and its elements keep the order they were written in.

## Literals

```femlang
let empty = []
let numbers = [1, 2, 3, 4]
let mixed = [1, "two", true, null]
let nested = [[1, 2], [3, 4]]
```

A trailing comma before the closing bracket is allowed
(`[1, 2, 3, ]`). List literals can appear anywhere an expression can: as
declaration values, call arguments, loop operands, and so on. Bare `[1, 2` (
missing `]`) is a parse error (`expected ']' after list elements`).

## Indexing

`xs[i]` reads the element at a zero-based index. Indexing is a postfix
operator, so it binds tighter than unary and binary operators and chains
left-to-right:

```femlang
let numbers = [1, 2, 3, 4]
print(numbers[0])         # 1
print(numbers[3])         # 4
print(numbers[1] + numbers[2])   # 5

let grid = [[1, 2], [3, 4]]
print(grid[1][0])         # 3
```

The index must be an integer, and it must satisfy `0 <= i < len`; otherwise
the program stops with a runtime error. The failure messages are
`index expression requires an integer` (non-integer), `list index out of
bounds`, `string index out of bounds`, and `cannot index this value` (an
operand that is neither a list nor a string).

Reading an index returns a fresh copy of the element, not a reference to the
stored value, so the result is an ordinary independent value.

### Indexing strings

Strings are indexable with the same `[i]` syntax and bounds rules. The result
is a new one-character string:

```femlang
let word = "Fem"
print(word[0])            # F
print(word[2])            # m
```

## Iterating a list: `for x in xs:`

The `for` loop accepts a list operand. When the header has no `..` range, the
loop visits each element in order, binding the loop variable to a copy of it:

```femlang
mut total = 0
for n in [1, 2, 3, 4]:
    total = total + n
print(total)              # 10
```

As with range loops, the loop variable is an implicit `mut` bound in the
current environment and keeps its last value after the loop, and
`break`/`continue` (and their `slay`/`skip` aliases) work the same way. Empty
lists run the body zero times. A non-list operand is a runtime error
(`for in requires a list`).

Nested lists iterate element by element, so nested `for` loops walk a grid:

```femlang
mut total = 0
for row in [[1, 2], [3, 4]]:
    for cell in row:
        total = total + cell
print(total)              # 10
```

Range loops are unchanged; a `..` in the header always selects the integer
range form (`for i in 0..n`).

## Equality

`==` and `!=` compare lists by structure: two lists are equal when they have
the same length and their corresponding elements are equal, recursing into
nested lists. Comparing a list with a value of another type is simply unequal
(no error):

```femlang
print([1, 2] == [1, 2])   # true
print([1, 2] == [2, 1])   # false
print([[1], [2]] == [[1], [2]])   # true
print([1] == 1)           # false
```

## Truthiness and the standard library

A list is truthy when it is non-empty and falsy when empty, so it can be used
directly as an `if` or `while` condition:

```femlang
if [0]:
    print("non-empty is true")
```

- `len(xs)` returns the element count (`len([])` is `0`).
- `type(xs)` returns `"list"`.
- `str(xs)` and `print(xs)` render the list as `[a, b, c]`, using each
  element's display form (strings appear without quotes, nested lists recurse).

```femlang
print(len([1, 2, 3]))     # 3
print(type([]))           # list
print(str([1, "a", true]))   # [1, a, true]
```

## Immutability

Lists are immutable in this milestone: there is no element assignment
(`xs[0] = ...`), and no list methods. Every read produces a copy, so list
values flow through the language exactly like strings and other owning values
(deep-copied on assignment, cloned when read from the environment).

See `examples/lists.fem` for a runnable tour.
