# FemLang repository audit (Phase 0)

Date: 2026-09-25. Audited commit: `28219b0` ("Consolidate verified baseline: fix
regression suite, safe reassignment, ownership, and native registry"), branch
`phase-00-repository-audit`.

The purpose of this document is to record the current, verifiable state of the
repository before further milestones: what the code is, how it is built and
tested, what is known to be missing or inconsistent, and the recommended order
in which to address it. It is a durable record, not a plan that must be
followed literally.

## Current files (35 tracked)

```
.gitignore                         Editors helpers
LICENSE                            editors/vscode/README.md
Makefile                           editors/vscode/language-configuration.json
examples/basic.fem                 editors/vscode/package.json
examples/control_flow.fem          editors/vscode/syntaxes/femlang.tmLanguage.json

include/       src/                docs/                tests/
  ast.h          ast.c               baseline-validation.md  test_runner.c
  evaluator.h    eval_main.c         control-flow.md
  lexer.h        evaluator.c         milestone-report.md
  native.h       lexer.c             overview.md
  parser.h       main.c              ownership.md
  token.h        native.c            parser-roadmap.md
  value.h        parse_main.c        reassignment.md
                 parser.c            testing.md
                 token.c
```

The repository contains a working tree-walking interpreter for a small,
Python-style, indentation-sensitive language (FemLang): lexer, Pratt parser,
AST, evaluator, environment, and an ownership-safe native function registry.

## Build and verification (verified this session)

| Command | Result |
| --- | --- |
| `make` | `exit 0`; builds `femlang` (rejects CR/CRLF sources with strict flags `-std=c11 -Wall -Wextra -Wpedantic -Werror -Iinclude`) |
| `make test` | `exit 0`; builds and runs `femlang-tests`; prints `All FemLang tests passed.` |
| `cc -std=c11 -Wall -Wextra -Wpedantic -Werror -Iinclude -fsyntax-only src/main.c` | `exit 0` |
| `cc -std=c11 -Wall -Wextra -Wpedantic -Werror -Iinclude -fsyntax-only src/parse_main.c` | `exit 0` |
| `cc -std=c11 -Wall -Wextra -Wpedantic -Werror -Iinclude -fsyntax-only tests/test_runner.c` | `exit 0` |
| `gcc -fanalyzer` (all sources) | clean, no findings |

`CC`, `CFLAGS`, and `LDFLAGS` are overridable (`?=`). Makefile targets:
`all`, `test`, `asan`, `clean`.

## Test inventory

`tests/test_runner.c` (864 lines, run via `make test`) exercises:

- lexer: keywords and playful aliases (`spark`/`serve`/`slay`/`skip`),
  identifiers, integers/floats, strings and escapes, unterminated string
  errors, operators, newline/EOF handling, indentation/dedentation (flat,
  nested, at EOF), blank and comment lines, inconsistent-indentation errors,
  trailing comments;
- parser: immutable and mutable declarations, assignment (incl. precedence),
  string literal content and escapes, arithmetic and identifier precedence,
  `if`/`else`, multi-statement blocks, and three invalid-input cases;
- evaluator: declarations, `mut` and successful/immutable/undefined
  reassignment, repeated string reassignment, string concatenation, string
  equality, value equality/comparisons (`==`, `!=`), string ownership (no
  shared buffers), `if`/`else` branches, integer arithmetic, division by zero,
  undefined lookup;
- native registry: register/lookup/replace/grow, null-name and null-fn
  rejection, callback invocation, idempotent cleanup;
- value ownership: clone independence, double-free safety, free resets to null.

## Toolchain and sanitizer status

Environment: Windows, w64devkit (GCC 16.2.0, `cc`), GNU Make 4.4.1, Git
2.55.0.windows.5.

- `make asan` **cannot link on this machine**:
  `C:/w64devkit/bin/ld.exe: cannot find -lasan` / `-lubsan`; only the classic
  MinGW runtime is bundled. This is a toolchain limitation, not a code defect.
- Sanitizer runs must be done on a toolchain that ships ASan/UBSan (Linux,
  macOS, or MSYS2/clang). Until then, `-fanalyzer` is the substitute; it is
  clean.

## Known compilation problems

None in the sources that form the build. The two intentionally unbuilt tools
(`src/main.c`, `src/parse_main.c`) compile cleanly under the strict flags.
The only blocked target is `asan` (see above).

## Duplicate or stale files

| File | Status | Recommendation |
| --- | --- | --- |
| `src/main.c` | Legacy lexer-only demo (prints tokens); superseded by `src/eval_main.c`; not in the Makefile. | Keep untouched; decide later whether to delete or move to `tools/` and repurpose as `femlang tokens`. |
| `src/parse_main.c` | Developer AST-printer (`femlang ast`); not in the Makefile; duplicates `read_source_file` from `eval_main.c`. | Keep; reuse when a `femlang ast` subcommand lands; consider a shared `src/file_io.c` helper then. |
| `docs/overview.md` | Stale: describes the repo as lexer-only and treats aliases as "could include"; the milestone-report, ownership, testing, control-flow and parser-roadmap docs are the current ones. | Rewrite or fold into `README.md` in a later phase (do not delete the roadmap list). |
| `src/evaluator.c` | Contains the entire value system (`value_int`, `value_*`, `print_value`) even though `include/value.h` reads as a standalone module. | A `src/value.c` module split is a medium refactor; deferred. |

No file duplicates are stale enough to break the build; the duplication is
organizational.

## Missing declarations / symbols

- The built objects link cleanly: everything declared in `include/*.h` is
  defined, and there are no unused-but-required externals.
- `src/value.c` does not exist (value functions live in `src/evaluator.c`,
  declared in `include/value.h`) — see the stale-files table.
- No headers exist yet for diagnostics, bytecode, or a VM: that is expected
  ahead of the compiler/VM milestone, not a defect.
- `docs/overview.md` mentions a bytecode/VM pipeline that does not exist yet.
- The VS Code extension (`editors/vscode`) is structurally valid (package.json,
  language-configuration.json, TextMate grammar) but lacks `indentationRules`
  and a `.vscode/launch.json`; it must be run via "Extension Development Host".

## AST, parser, and evaluator mismatches

These are the gaps between what the lexer produces, what the parser builds,
what the evaluator handles, and what the tests cover. All are documented facts
from source inspection:

| Gap | Location | When it bites |
| --- | --- | --- |
| `AST_CALL` exists and is freed/printed, but the parser never creates it and the evaluator returns `value_null()` unconditionally. | `include/ast.h`, `src/ast.c:64`, `src/parse_main.c:66`, `src/evaluator.c:557-559` | The core missing feature for Phase 5 (call expressions). Native registry (`src/native.c`) is ready. |
| `AST_LIST` exists and is freed, but is never parsed and evaluates to `value_null()`. | `include/ast.h`, `src/evaluator.c:561-562` | Lists (Phase 10). |
| Comparison operators `< <= > >=` lex and parse (precedence table in `src/parser.c:295-296`) but the evaluator has no numeric comparison branch: ints fall into the integer default and report `integer arithmetic error` (`src/evaluator.c:513-523`); floats return null silently (`src/evaluator.c:542-548`). | equality was fixed to dispatch first (`src/evaluator.c:431`); comparisons were not | Any ordering comparison. |
| Float `%` returns null silently (not in float `switch`). | `src/evaluator.c:526-548` | `3.5 % 2.0`. |
| Unary minus on `INT64_MIN` is unguarded signed overflow (UB). | `src/evaluator.c:411-415` | Practically unreachable today: the positive literal `9223372036854775808` fails int64 lexing first. Must be guarded when sanitizers run. |
| `fn`, `elif`, `for`, `in`, `while`, `break`, `continue`, `try`, `catch`, `finally`, `match` all lex but have no AST, parser branch, or evaluator handling; using them today is a parse error. `TOKEN_FN` and `TOKEN_ELIF` are therefore dead outside the lexer. | `include/token.h`, `src/lexer.c`, `src/parser.c` | Phases 5 (fn) and 8-13. |
| Mixed-type binary operations and string vs non-string comparisons evaluate to null without a diagnostic in several paths. | `src/evaluator.c` | Cosmetic; should become a proper type-error message. |

## Phase 5 resolution (2026-09-25)

Phase 5 (call expressions) resolved several entries above; this section
records the disposition:

| Audit entry | Disposition in Phase 5 |
| --- | --- |
| `AST_CALL` dead (never parsed; evaluator returned null) | **Resolved.** Parser builds `AST_CALL` (postfix `call`/`call_tail`, `parse_arguments` in `src/parser.c`); evaluator invokes the registry (`src/evaluator.c` `case AST_CALL`). |
| Call expressions not wired to the registry | **Resolved.** `Environment` owns a `FemNativeRegistry` (`env_native_registry()`); identifiers resolve to `VALUE_NATIVE` after env miss; `VALUE_NATIVE` added to `include/value.h` (borrowed callback pointer). |
| Comparisons `< <= > >=` unhandled | **Resolved.** Ordered comparisons on two ints or two floats return bool; other types report `comparison requires two numbers`. |
| Float `%` returns null silently | **Resolved.** Float remainder via `fmod` (truncated, sign of dividend); both float `/` and `%` by `0.0` report `division by zero`. |
| Unary `-` on `INT64_MIN` unguarded | **Resolved.** Guarded: reports `integer arithmetic error` (still unreachable via literals, now safe for sanitizers). |
| Mixed-type binary ops silent null | **Resolved (inconsistent path).** Unsupported/mixed ops report `cannot apply operator '<op>' to these values`; `==`/`!=` on different types still returns false by design. |
| `fn`, `elif`, loops, `try`/`match` lexed but dead | **Unchanged.** `TOKEN_FN` is the Phase 7 target; everything else follows Phases 8-11. |
| `AST_LIST` dead | **Unchanged.** Phase 10. |
| Int division-by-zero error text | **Changed.** Int `/` and `%` by zero now report `division by zero` (was folded into `integer arithmetic error`). |

New public surface from Phase 5: `VALUE_NATIVE`, `value_native()`,
`env_native_registry()`; bundled `print` native registered in `src/eval_main.c`;
example `examples/calls.fem`. Since this phase the evaluator also reports a
`Runtime error:` text for every failing program instead of silently printing
`null` for several operator cases.

## Ownership model concerns

Design (documented in `docs/ownership.md` and `docs/baseline-validation.md`):

- Values are owned by exactly one holder; `value_clone` deep-copies; freeing a
  value resets it to null (double-free safe).
- `env_lookup` returns a *borrowed* pointer; callers must clone if they keep
  it beyond the current evaluation step.
- `env_define`/`env_assign` clone the name value pair; assignment clones the
  new value before releasing the old one, so a mutated binding does not alias
  any other binding.
- Native registry owns a copy of each function name and invalidates/replaces
  on collision; `native_registry_free` is idempotent.

Remaining concerns:

1. The value functions living in `evaluator.c` make the "single owner" rule
   easy to violate by accident during future work; a `src/value.c` split
   (already staged layout in `include/value.h`) reduces that risk.
2. Borrowed lookups are enforced by convention only; a future diagnostics pass
   or code review should confirm no persisted borrows exist (current code is
   correct, and `test_eval_string_ownership` locks the behavior).
3. ASan/UBSan have never actually been executed (toolchain), so the UB claim
   about unary minus is a code-inspection finding, not a runtime one.

## Recommended phase order

Dependency-driven, cheap-first:

1. **Phase 5 — call expressions.** Parser call grammar, `AST_CALL`
   evaluation wiring the existing native registry (a callable value or
   direct native dispatch), first real ues of `TOKEN_FN`. Fold in the quick
   semantic fixes while touching the evaluator: numeric comparisons, float
   `%`, and the `INT64_MIN` unary guard plus a proper mixed-type error.
2. **Phase 6 — native standard library.** `print`, `len`, etc. on top of the
   registry; `docs/overview.md` rewrite and `src/parse_main.c`/`src/main.c`
   triage (`femlang tokens | ast | run`).
3. **Phase 7 — functions.** `AST_FN`, function values, local environments,
   recursion (loop guard), closure semantics decision (heap-backed envs?).
4. **Phase 8 — `elif`** (token exists) and statement/block polish including
   the type-error messages above.
5. **Phase 9 — loops** (`for in`, `while`, `break`, `continue`; all tokens
   exist).
6. **Phase 10 — lists** (`AST_LIST` evaluation, `[..]` literals, indexing).
7. **Phase 11 — error handling** (`try`/`catch`/`finally`, `match`).
8. **Later — compiler/VM milestone** (bytecode format, `src/vm.c`,
   diagnostics interface) as originally outlined in `docs/overview.md`.

Original roadmap items 1-4 of `docs/overview.md` (tokenizer tests, parser/AST
definitions, tree-walking evaluator) are complete; items 4-8 are Future Work.

Phase 5 (2026-09-25, branch `phase-05-call-expressions`, commit `4496cf0`)
landed call expressions per step 1 and folded in the quick semantic fixes
(ordered comparisons, float `%`, `INT64_MIN` guard, mixed-type errors).

Phase 6 (2026-09-25, branch `phase-06-stdlib`) landed the native standard
library per step 2's library half: `print`, `len`, `int`, `float`, `str`,
`type` registered via `fem_stdlib_register()` (`src/builtins.c`), the new
transient `VALUE_ERROR` value type so natives report errors through the
environment channel, and the public `value_to_string()` display helper.
The step-2 CLI triage (`femlang tokens | ast | run`) and `docs/overview.md`
rewrite were intentionally deferred: `overview.md` got a light current-state
refresh, and the CLI subcommands stay out of scope until the parser/evaluator
surface stabilizes further.

## Evidence (this session)

- `make clean && make && make test` → `All FemLang tests passed.` (exit 0).
- Standalone strict-flag syntax checks of `src/main.c`, `src/parse_main.c`,
  `tests/test_runner.c` → exit 0 each.
- `make asan` → failure at link: `cannot find -lasan`, `cannot find -lubsan`
  (external toolchain limitation; exit 2).
- Git: local `master` (`28219b0`) tracks `origin/main` at `28219b0`; history:
  remote canonical commits `84ec93d`..`7ec6d26` (whose HEAD failed the suite
  at `tests/test_runner.c:73`) plus the fast-forward consolidation
  `28219b0`. Push uses SSH (`git@github.com:oorr44aaccllee/FemLang.git`);
  no force-push or history rewrite was performed.
- Environment: Windows; GCC 16.2.0; GNU Make 4.4.1; Git 2.55.0.windows.5.