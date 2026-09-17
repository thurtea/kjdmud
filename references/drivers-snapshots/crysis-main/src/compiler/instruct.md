# src/compiler/ - Lexer, Parser, CodeGen, AST

## What lives here

| File | Role |
|------|------|
| `Lexer.cpp` + `include/.../Lexer.hpp` | Hand-written tokenizer. Produces `Token` stream for Parser. |
| `Parser.cpp` + `include/.../Parser.hpp` | Recursive-descent parser. Consumes Token stream, emits `AstPtr` tree. |
| `CodeGen.cpp` + `include/.../CodeGen.hpp` | AST → bytecode `CompiledProgram`. Emits `OpCode` sequences into `FunctionEntry` byte vectors. |
| `include/.../Ast.hpp` | All AST node types (`BinaryExpr`, `CallExpr`, `InlineLambdaExpr`, …). |

The compiler is invoked by `ObjectManager::compile()` for every `.c` file the
driver loads. It runs the system `cpp` preprocessor first (with FluffOS-
compatible predefined macros), then tokenizes, parses, and generates bytecode
in one pass.

## Files to read before touching this directory

- `include/amlp/compiler/Ast.hpp` - full AST node catalogue
- `include/amlp/compiler/Lexer.hpp`
- `include/amlp/vm/Bytecode.hpp` - the opcodes CodeGen emits
- `ROADMAP.md` Phase 0 and Phase 1 rows that touch `src/compiler`
- `temp/reference/fluffos-2.9-ds2.08/grammar.y` - the reference grammar
- `temp/reference/fluffos-2.9-ds2.08/lex.c` - the reference lexer

## Phase 0 tasks

None directly in the compiler. The stub/gap work is in `src/efun` and
`src/vm`. However:
- Verify that `#include` preprocessing handles all macros the mudlib uses
  before Phase 1 begins.

## Phase 1 tasks - Dialect-aware frontend

**This is the biggest compiler change in the roadmap. Do not start until
Phase 0 is complete and the test suite is green.**

**Rescoped 2026-08-18 (scoping pass only, nothing implemented yet: see
ROADMAP.md rows 1.2/1.3 for the full citation trail this section
summarizes):** the plan below had several load-bearing errors, corrected
here by reading the real grammar/lexer sources directly
(`temp/ldmud/src/prolang.y`, `temp/ldmud/src/lex.c`,
`temp/ldmud/src/func_spec`, `temp/dgd/src/comp/parser.y`).

### 1. Make the Lexer dialect-aware

The `Lexer` must accept a `LpcDialect` value (from `src/dialect/`) and
change its tokenization rules accordingly. **Neither `Lexer` nor `Parser`
takes one today**, confirmed via both constructors and their one real
call site, `ObjectManager::compile()`, this is the genuine prerequisite
everything else here sits on top of, and the proposed first slice
(ROADMAP.md row 1.2/1.3 scoping note) is exactly this plumbing, added
with zero behavior change before any real new keyword lands.

**FluffOS/MudOS (current default)**
- `(: … :)` closure literals - already working
- `(*fp)(args)` dereference-call syntax - already working
- No changes needed for the default dialect

**LDMud additions**
- `#'name` - real (`L_CLOSURE` in `temp/ldmud/src/lex.c`'s own
  `closure()`), but far richer than "a name after `#'`": roughly 50
  operator spellings, `#'[...]` index/range/map-index forms, `#'({`
  aggregate, `efun::`/`sefun::`/`lfun::`/`var::` prefixes, and `::`
  inherited-function references, all one token kind. **Blocked on a real
  architecture problem specific to this driver, not just a lexer
  addition:** `ObjectManager::compile()` shells out to the real system
  `cpp -x c` before the Lexer ever runs, and standard GCC `cpp`
  hard-errors on any line whose first non-whitespace character is `#`
  and isn't a real directive: a bare `#'this_player;` statement on its
  own line would fail the whole file's preprocessing before this driver's
  own tokenizer ever sees it. Needs a real decision (mask `#'`-shaped
  lines before `cpp` sees them and restore after, or stop using real
  `cpp` for LDMud-dialect files, or something else) before this can be
  scheduled as ordinary lexer work.
- `'name` (bare leading quote) - real and **previously undocumented
  anywhere in this repo**: a distinct `L_SYMBOL` token producing an LPC
  `symbol` value (used as `lambda()`'s own parameter-name convention,
  `'x`, and by `symbol_function()`/`symbol_variable()`/`symbolp()`).
  This driver's `Value` variant has no `symbol` member at all: needs a
  `src/vm` decision first (row 1.9's neighbor problem: a token with
  nowhere real to go yet).
- ~~The `lambda` keyword~~ / ~~`unbound_lambda` keyword~~ / ~~`bind`
  keyword~~ - **wrong, removed entirely.** `temp/ldmud/src/func_spec`:
  `closure lambda(null|mixed *, mixed);` / `closure unbound_lambda(...)`
  are ordinary efuns (the same mechanism already confirmed for
  `bind_lambda()` and `replace_program()`, rows 1.5-1.7): no keyword,
  no special lexer handling, nothing for this row. `bind()` doesn't exist
  as an LDMud name at all (row 1.7's own correction); the real name,
  `bind_lambda()`, is likewise a plain efun call.

**DGD additions**
- `nil` keyword - confirmed real (`temp/dgd/src/comp/parser.y`'s own
  `NIL` token, `Node::createNil()`), maps to a new `Value::Nil`-shaped
  variant member - see `src/vm`. Small: one new keyword, ordinary literal
  shape, but a real (if small) CodeGen coupling to row 1.10 the moment it
  exists as a token.
- `atomic` function modifier keyword - confirmed real
  (`temp/dgd/src/comp/parser.y`'s own `ATOMIC` token, same
  `non_private` modifier-list production as `STATIC`/`NOMASK`/
  `VARARGS`). The smallest real addition found in this whole pass: this
  driver's own `kKeywords`/`modifierKeywords` mechanism already handles
  exactly this shape generically.
- `rlimits` statement keyword - confirmed real, but **wrong grammar**:
  not `rlimits(ticks : stack)`. Real DGD (`temp/dgd/src/comp/
  parser.y:566-582`): `RLIMITS '(' expr ';' expr ')' compound_stmt` --
  semicolon, not colon, and the first expression is the **stack** limit,
  the second is **ticks** (confirmed by the real error message order).
  Real shape: `rlimits (stack_expr; ticks_expr) { body }`.
- `parse_string` - handled as an efun, no lexer change needed
  (unrelated to `parse_*`, row 0.13a's FluffOS package)
- Newly found, not previously tracked anywhere: DGD's own closure/
  function-pointer syntax is a third, distinct family from FluffOS's
  `(: :)` and LDMud's `#'`: `&ident(args)` / `&(*cast)(args)` (a "call
  template"), plus `->`/`<-` for DGD's own persistent-object-call and
  inherited-super-call conventions. Zero DGD lexer work exists for any of
  this yet; flagged for whenever DGD's own dialect work is picked up, not
  sized here.

Concretely, once the plumbing above lands: extend `kKeywords` (Lexer) and
`modifierKeywords` (Parser, `Parser.cpp:70,80-83`) per dialect for
`atomic`/`nil`; everything else above needs its own new lexer routine
(`#'`, `'name`) or new grammar inside an existing literal parse path
(mapping width, `rlimits`), not a one-line keyword-set addition.

**Implemented 2026-08-18 (greenlit first slice, ROADMAP.md rows
1.2/1.3):** the plumbing plus `atomic` alone, exactly as proposed above,
nothing else. `Lexer`/`Parser` both now take an `LpcDialect dialect`
constructor parameter (`LpcDialect::FluffOS` default, confirmed zero
behavior change for every pre-existing call site). `atomic` is
recognized as `TokenType::Keyword` only when `dialect_ ==
LpcDialect::DGD` (`Lexer::lexIdentOrKeyword()`), and accepted as a real
function-declaration modifier only under the same condition
(`Parser::isModifierKeyword()`, reusing `parseDeclPrefix()`'s existing
generic modifier-consumption loop unchanged, plus excluded from
`isTypeKeyword()`'s default-true classification so it can never be
misread as a type). `nil`, `#'`, `'name`, mapping width, and `rlimits`
all remain exactly as scoped above: none of them touched. 2 new
regression tests (`test_lexer.cpp`) cover the Lexer-level token
classification directly and a full end-to-end compile of the same
source under all three dialects.

**`nil` implemented 2026-08-18 (continued), the next greenlit slice,
same gated-per-dialect pattern:** read `temp/dgd/src/comp/parser.y`'s
own `"NIL { $$ = Node::createNil(); }"`, `temp/dgd/src/comp/node.cpp`'s
`Node::createNil()`, and `temp/dgd/src/data.h`'s own `T_NIL`/
`VAL_TRUE`/`VAL_NIL` macros directly before writing anything: see
ROADMAP.md row 1.10 for the full citation trail, including the real
strict-typechecking nuance (`temp/dgd/src/data.cpp`'s own `"nil.type =
(stricttc) ? T_NIL : T_INT;"`) this implementation targets. `nil` is
now a real, distinct `Value` (`Nil`, a stateless struct in
`ValueVariant`), lexed as `TokenType::Keyword` only under
`LpcDialect::DGD` (same `Lexer::lexIdentOrKeyword()` gate as `atomic`),
parsed as a new `NilLiteral` AST node in `parsePrimary()` (independently
dialect-gated there too, not solely trusting the Lexer), and compiled
via a new `OpCode::PushNil` (no operand). Stayed genuinely minimal, not
bigger than expected: `isTruthy()`/`valuesEqual()` needed one explicit
case each; every arithmetic/comparison opcode already threw a clear
type error for `Nil` with zero further changes, since
`asArithmeticOperand()` never special-cased anything but
`int64_t`/`double`/`std::monostate` to begin with. 4 new regression
tests confirm end to end (compiles and evaluates correctly under
`dialect: dgd`; fails to compile under `fluffos`/`ldmud`, this time via
`CodeGen::resolveVariable()`'s "undeclared variable" error rather than a
parse error, since a bare `nil` identifier is syntactically valid there
unlike `atomic`). `#'`, `'name`, mapping width, and `rlimits` remain
untouched.

### 2. Make the Parser dialect-aware

`Parser` already takes a `Lexer`; add a `LpcDialect` parameter (see the
plumbing note above: this is genuinely one parameter threading through
one real call site today, `ObjectManager::compile()`).

**LDMud additions**
- Parse `#'name` (and its full real grammar above) into a new
  `LambdaRefExpr`/`ClosureLitExpr` AST node that CodeGen turns into a
  `MakeClosure` instruction. Blocked on the preprocessing-pipeline issue
  above before this is worth scheduling.
- Parse `'name` into a new `SymbolLiteral` AST node. Blocked on the new
  `Value` variant member (`src/vm`) this needs to mean anything.
- ~~Parse `lambda(({ params }), body_array)` into a `LambdaExpr` AST
  node~~ / ~~`unbound_lambda(...)` similarly~~ - **wrong, removed.** See
  the Lexer section above: these are ordinary function calls (array
  literal argument, no new grammar), resolved entirely in `src/efun` +
  `src/vm` once a real `lambda`/`unbound_lambda` efun exists to build a
  closure from the array-encoded body.
- ~~Parse `replaces` as an optional qualifier on `inherit "path";`.~~
  Resolved 2026-08-17 (ROADMAP row 1.6, rescoped): real LDMud has no
  `replaces` token anywhere in `inherit`'s own grammar: re-grepped
  `temp/ldmud/src/prolang.y`'s `inheritance_qualifier`/
  `inheritance_modifier` productions directly. Nothing for the Parser to
  add here; the real divergence is `replace_program()`'s own zero-argument
  form, an efun concern (`src/efun/EfunTable.cpp`), not a parser one.
- Mapping width: **wrong syntax, corrected.** Not `([ k:v1, v2, v3 ])` --
  commas already separate different key entries, so they cannot also
  separate same-key values. Real (`temp/ldmud/src/prolang.y`'s own
  `m_expr_list2`/`m_expr_values`, lines 17224-17256): values for the
  *same* key are semicolon-separated, `([ "a": 1; 2; 3, "b": 4; 5; 6 ])`;
  every entry in one literal must have the same width (a real semantic
  check: "Inconsistent number of values in mapping literal"). A second,
  simpler literal exists for an empty mapping of given width:
  `([: width_expr ])` (`F_M_ALLOCATE`), not previously documented here at
  all. ~~Moderate: self-contained inside the existing mapping-literal
  parse path, no new AST node kind beyond a width field.~~ **Corrected
  2026-08-18, investigated as a candidate slice after `atomic`/`nil`:
  genuinely bigger, not moderate.** The literal grammar alone is exactly
  this small, but real LDMud mappings are N-columns-wide at the *value*
  level too (`m_allocate`/`m_values`/`m_entry`/`m_reallocate`/`m_add`/
  `m_contains`, none implemented), and this driver's own `Mapping`
  (`std::vector<std::pair<Value, Value>>`) has no width dimension at
  all, load-bearing throughout `MakeMapping`/`Index`/`IndexAssign`/
  `sizeof`/`map_delete`/mapping `+`/save-restore. A parser-only slice
  that silently discarded extra values would not "work end to end". See
  ROADMAP.md row 1.9 for the full finding, it now owns both literal
  syntaxes, not split from the runtime side across two rows.

**DGD additions**
- Parse `atomic` as a function-declaration modifier (analogous to
  `nomask`) - confirmed real, reuses the existing `modifierKeywords`
  consumption loop verbatim, no new AST node needed at all. The
  recommended first real per-dialect token (see ROADMAP.md's scoping
  note for the full "why").
- Parse `rlimits (stack_expr; ticks_expr) { body }` as a `RlimitsStmt`
  AST node - corrected grammar above (was `(ticks : stack)`, wrong
  separator and wrong argument order).
- Parse `nil` as a `NilLiteral` AST node (not `IntLiteral{0}`) - confirmed
  real, ordinary literal shape, coupled to row 1.10 for what CodeGen
  actually emits.

### 3. Make CodeGen dialect-aware

- `LambdaRefExpr`/`ClosureLitExpr` (`#'...`) → `MakeClosure` opcode with
  the appropriate closure kind (blocked on the preprocessing-pipeline
  decision above)
- `SymbolLiteral` (`'name`) → needs a real `Value` variant member first
  (`src/vm`); no opcode to design until that lands
- `RlimitsStmt` → new `PushRlimits` / `PopRlimits` opcodes (see `src/vm`),
  corrected grammar above
- `atomic` modifier → mark `FunctionEntry::isAtomic = true` (see
  `src/vm`, row 1.12's own separate VM-level concern, landing the
  keyword alone is inert until then)
- `NilLiteral` → `PushNil` opcode, **implemented 2026-08-18, real, not
  a placeholder** (see the row 1.10 note below)
- Mapping width literals (`([ k: v1; v2 ])`, `([: N ])`), **investigated
  2026-08-18, not implemented, bigger than expected** (see the row 1.9
  note above, under "Mapping width", CodeGen work here is inseparable
  from the `Mapping`-structure rework row 1.9 owns)

Explicitly **removed** from this section, not real: `LambdaExpr`/
`UnboundLambdaExpr` → `MakeClosure` (`lambda()`/`unbound_lambda()` are
ordinary efuns, `src/efun` + `src/vm` territory, not CodeGen's).

## Phase 2 tasks

### async/await (Phase 2.6)

- Add `async` as a function-declaration modifier (like `nomask`/`varargs`).
- Add `await expr` as a statement/expression.
- CodeGen: `await` emits a new `Suspend` opcode that the VM/scheduler handles
  as a coroutine yield point.

## Testing

All compiler tests currently live in `test/test_lexer.cpp`. New tests should
be added to that file or a new `test_parser.cpp` / `test_codegen.cpp`.

```bash
ctest --test-dir build -R "lexer|parser|codegen" --output-on-failure
```

## Key invariants

- The compiler must never crash on malformed input - always throw
  `LpcRuntimeError` (from `src/core`) with a clear source/line/column message.
- Adding a new keyword must not break any existing mudlib file that uses that
  word as a plain identifier. Check first. The comment in `Lexer.cpp` about
  `"array"` is the canonical example of this hazard.
- CodeGen changes must be accompanied by a corresponding `OpCode` addition in
  `Bytecode.hpp` AND a `VM::run()` case for it AND a regression test.
