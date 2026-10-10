# Loops

FemLang ships two loop forms plus the loop control statements `break` and
`continue`.

## `while`

```femlang
mut n = 5
while n > 0:
    print(n)
    n = n - 1
```

The condition is evaluated before every iteration; when it is false the loop
ends. A `while` loop with a condition that is immediately false never runs its
body.

## `for ... in ...`

`for` walks an integer range. The bounds can be any expressions, but both must
evaluate to integers:

```femlang
for i in 1..6:
    print(i)          # 1, 2, 3, 4, 5
```

The upper bound is exclusive (like Python's `range` with a single upper
argument). The range walks downward when the lower bound is greater:

```femlang
for i in 5..1:
    print(i)          # 5, 4, 3, 2
```

`low == high` yields no iterations. Trying to iterate over non-integers is a
runtime error (`for range requires two integers`).

The loop variable is bound in the current environment, behaves like an
implicit `mut`, and keeps its last value after the loop:

```femlang
for i in 1..4:
    i
print(i)               # 3
```

Reassigning the loop variable inside the body does not disturb the counter.

## `break` and `continue`

- `break` leaves the innermost loop immediately (the loop does not run any
  further iterations or body statements).
- `continue` skips the rest of the current iteration: for `for` the counter
  still advances to the next value, for `while` the condition is re-checked.

```femlang
mut acc = 0
for x in 1..20:
    if x == 3:
        continue
    if x > 5:
        break
    acc = acc + x
print(acc)             # 1 + 2 + 4 + 5 = 12
```

`break`/`continue` must appear lexically inside a loop body. Elsewhere they
are runtime errors (`break outside a loop`, `continue outside a loop`) — this
includes a `break` written inside a function, because the function has its own
frame that is not inside a loop. The playful aliases `slay` (`break`) and
`skip` (`continue`) are full synonyms.

## Returning from inside a loop

`return` inside a loop propagates out of the loop and out of the enclosing
function as usual:

```femlang
fn find_power(t):
    for i in 1..10:
        if i * i == t:
            return i
    return -1

print(find_power(49))  # 7
```

## Interaction with blocks and errors

- A `break`/`continue` written inside an `if` block inside a loop still
  applies to the enclosing loop; statements after the `if` in the loop body
  are skipped (the loop body block stops scanning once a control flag is set).
- Nested loops: `break`/`continue` always target the innermost loop. Flags are
  consumed by that loop, so the outer loop continues unaffected.
- A runtime error in a loop condition or body stops the program through the
  normal error channel.

See `examples/loops.fem` for a runnable tour.