# STATUS

**2026-09-04 (a further session, continuing further yet once more):
`CallOtherExpr` gets a real `argIsSpread`, the named leftover on Dead
Souls 3.8.2's `lib/lib/magic.c:85` `spell->eventParse(this_object(),
args...)`.** 911 tests total (908 plus 3), full suite green (direct
`build/test/amlp_tests` and `ctest` both 0 failures).

1. **Scoped first, as asked, before touching anything.** Re-read
   `Parser.cpp:1003`'s own comment and the original spread-operator
   session's STATUS.md entry (2026-09-04, "row 3.10's own named blocker
   ... is built"): item 3 there states plainly "`CallOtherExpr`
   (`->`/`call_other()`) and `sscanf()` deliberately reject a spread
   argument outright rather than silently mishandling it -- named, not
   implemented this slice." No architectural reason is given, only
   sequencing -- that session's own real corpus evidence at the time
   (`secure/sefun/events.c`'s `exclude = ({ exclude..., targets... })`)
   was array-literal and plain-call spread only; call-other spread had
   no real corpus site driving it yet. Confirmed a sequencing gap, not
   an architectural one, before writing anything.

2. **Real grammar and bytecode confirmed directly, not assumed from
   continuity.** `expr4 L_ARROW identifier '(' expr_list ')'`
   (`grammar.y:3580-3626`) uses the exact same `expr_list` nonterminal a
   plain call's argument list does -- the same production the `...`
   spread grammar rule (`expr_list_node: expr0 | expr0 L_DOT_DOT_DOT`,
   `grammar.y:2488-2496`) attaches to either way. Real `call_other`
   compiles through `validate_efun_call(arrow_efun, ...)`
   (`grammar.y:3614-3623`) into the same `F_SIMUL_EFUN`/efun-call opcode
   family (`interpret.c:3595-3602`'s own `F_SIMUL_EFUN`, reading
   `num_varargs` exactly like `interpret.c:3704`'s `F_EFUNV`) that
   `F_EXPAND_VARARGS` (`interpret.c:2680-2724`) already feeds for any
   other call. No structurally distinct runtime primitive exists for
   call_other's own argument list at all.

3. **This driver's own split confirmed narrow too.** `emitCallOtherExpr()`
   already compiles `target->func(args)` into `OpCode::CallEfun`
   (`calleeIdx` = "call_other", `argCount` = `2 + args.size()`) -- the
   identical opcode `CallExpr::forceEfun` already routes through, which
   `emitSpreadExpansions()`/`pendingVarargsDelta` already fully support
   (`VM.cpp`'s own `CallEfun` handler already reads `instr.argCount +
   pendingVarargsDelta`). `CallOtherExpr` (`Ast.hpp`) simply had no
   `argIsSpread` field to carry a spread flag through parsing to
   codegen at all -- a real, narrow wiring gap, not a structural one.

4. **Fix.** `CallOtherExpr::argIsSpread` (parallel `vector<bool>`, empty
   by default, covering only `args` -- never `target`/`function`, which
   stay separately typed fields with no real corpus evidence of ever
   being spread). Both `Parser.cpp` call_other sites populate it instead
   of throwing: the literal `call_other(target, function, ...args)` form
   slices `parsed.isSpread[2:]` for `args` (positions 0/1 are
   target/function -- still rejected outright, now with a precise
   "the target/function argument cannot be spread" message, since
   spreading either has no real corpus use and no slot in this driver's
   own AST shape to hold one); the `target->name(...)` operator form
   sets it directly (`parsed.isSpread` already lines up one-to-one with
   `args` there, no offset). `emitCallOtherExpr()` gained one
   `emitSpreadExpansions(callOther.argIsSpread)` call, in the same
   position `emitCallExpr()` already calls it -- right after emitting
   `args`, before the argCount-bearing instruction -- since
   `ExpandVarargs`'s own operand is a stack position counted back from
   the top (`VM.cpp`), unaffected by `target`/`function` already sitting
   deeper on the stack.

5. **Tests.** The exact `magic.c:85` shape (`target->name(fixedArg,
   xs...)`, the `->` operator form); the same primitive through the
   literal `call_other(target, "name", fixedArg, xs...)` function-style
   form (a structurally different `Parser.cpp` branch); and confirmation
   that spreading the target/function position itself in the literal
   form still throws a clean, specific error rather than silently
   misreading the array as the literal target/function value. All three
   verified as new failures against the pre-fix binary before the fix
   landed (the first two threw the old "not implemented yet" messages;
   the third's exact new message did not exist yet). Every pre-existing
   spread test (array-literal, plain-call, `efun::`, `CallParent`) and
   every pre-existing `call_other`/`->` test still pass unchanged.

6. **Dead Souls 3.8.2 boot re-attempted, live TCP, same method.** The fix
   confirmed live: `lib/lib/magic.c` now compiles completely, and boot
   progresses through `living.c`'s own remaining inherits and `player.c`
   itself. **New blocker reached, a different and unrelated bug, named
   here and stopped at rather than forced past, per this row's own
   standing discipline:** `secure/daemon/players.c`'s real `static void
   UserUpdate(string name, int status){...}` (raw line 728; the driver's
   own reported "line 708" is the same known include-splice
   line-counter offset artifact this row has already named before, not
   a misread) fails with `codegen`'s own "expected \")\" in function
   declaration parameters ... (got \"status\")". Root-caused with a
   minimal standalone reproduction (a throwaway `Lexer`+`Parser`-only
   scratch program linked directly against the already-built
   `libcompiler.a`, no scratch mudlib root or CMake target needed this
   time): this driver's own `Lexer.cpp` hardcodes `"status"` as an
   unconditional keyword (`kKeywords`, citing "lex.c: \"status\" is
   TYPE_NUMBER, a synonym for int"), so `int status` as a parameter name
   parses "int" as a complete unnamed parameter and then chokes on a
   second type keyword where a name or `)` was expected -- reproduced
   with a bare `void f(int status){}`, independent of `static` or
   parameter position. Real `lex.c:184`'s own `{"status", L_BASIC_TYPE,
   TYPE_NUMBER}` is gated behind `#ifdef HAS_STATUS_TYPE`, an optional,
   explicitly legacy flag ("old MudOS drivers had a 'status' type ...
   very archaic, but easy to support", `local_options.ds:148-153`'s own
   comment) -- and Dead Souls 3.8.2's own real, bundled build profile,
   `fluffos-2.23-ds03/local_options.ds:153`, explicitly `#undef`s it, the
   same file already cited as this project's own authoritative build
   config for this exact mudlib. So under Dead Souls' own real, intended
   build, `status` is not a keyword at all, and `players.c`'s own use of
   it as an ordinary parameter name is entirely real, valid LPC -- this
   driver's own hardcoded keyword is simply wrong for this build profile,
   the same class of bug as the earlier `ARRAY_RESERVED_WORD`/
   `REF_RESERVED_WORD`-style optional-keyword gaps this project has
   already fixed, not investigated further this turn since it is a
   different, unrelated bug from this row's own call-other-spread scope.

`docs/dev/ROADMAP.md` row 3.10 updated in place with this outcome.

**2026-09-04 (a further session, continuing further yet again): `staticClassTypeOf()`
walks an `IndexExpr`, the named leftover on Dead Souls 3.8.2's
`lib/lib/body.c:317` `Protection[i]->time`.** 908 tests total (905 plus
3), full suite green (direct `build/test/amlp_tests` and `ctest` both 0
failures).

1. **Real semantics confirmed against grammar first, not assumed.**
   `expr4 '[' comma_expr ']'` (real array/string index, `grammar.y:2913-
   2956`) computes `$$->type = $1->type & ~TYPE_MOD_ARRAY;` in the
   `default:` case whenever `$1->type & TYPE_MOD_ARRAY` -- the array bit
   is stripped, every other bit (including `TYPE_MOD_CLASS` and the
   packed class id, `grammar.y:648`'s own `$2->dn.class_num |
   TYPE_MOD_CLASS`) rides through unchanged. So `Protection[i]` genuinely
   has real static type "class MagicProtection", the identical type a
   plain `class MagicProtection x` variable has, resolved the same
   generic way any other array's element type is (no class-specific
   special case in real grammar at all). The later `expr4 L_ARROW
   identifier` production's `CLASS_IDX($1->type)` then resolves `->time`
   off that stripped type exactly as it would off a scalar. Confirmed
   this is a real static-type fact, not something resolved differently
   for arrays vs. scalars.

2. **This driver's own gap, confirmed narrow.** `CodeGen::
   staticClassTypeOf()` only ever walked a bare `VarRefExpr` (a
   variable's own declared class) or a `TypeCastExpr` (an explicit
   `(class Name)` cast) -- never an `IndexExpr`. STATUS.md's own prior
   2026-09-04 entry (the `(class Name)` cast slice) explicitly scoped
   this out at the time: "Not in this slice: walking an `IndexExpr` of a
   `class Name *` variable so `Marriages[0]->Spouse` would work *without*
   a cast. No real corpus site does that; they all write the cast." That
   premise is now overturned by `body.c:317` itself, a real corpus site
   with no cast. This driver's own `objectVarClassTypes_`/
   `localClassTypes_` symbol tables already store one class name per
   variable regardless of the array marker (`Parser.cpp`'s own
   `TypeToken` drops the array-ness flag the same way, `CodeGen.cpp:1575`
   stores the same string for both a scalar and an array declaration of
   the same class), so this was a real, narrow gap in the AST walk
   itself, not a wider representation problem -- exactly the scope
   boundary check called for before touching anything.

3. **Fix.** `staticClassTypeOf()` now recurses into `IndexExpr::target`
   when the index is an ordinary single-element index (not a range
   slice, which stays an array rather than an element; not a
   `MemberNameMarker`, which is a chained `->member` result whose own
   type this driver does not track, so it stays unresolved rather than
   silently guessed). No VM/opcode change: `emitIndexValue()` already
   did the real work (resolving a `MemberNameMarker` against a static
   class name into the member's `PushInt` index) for a bare `class Name`
   variable; this only widens what `staticClassTypeOf()` will accept as
   the target expression feeding it.

4. **A second, independent gap found live at the same real line, fixed
   the same session.** Re-attempting the boot after (3) still failed at
   the same statement with a generic `codegen: unsupported expression
   kind` (no file/line, this driver's AST carries none). Bisected by
   compiling `body.c` standalone against a scratch mudlib root
   (symlinked to the real `temp/ds3.8.2_extracted/ds3.8.2/lib` tree,
   `secure`/`include` shared, `lib/body.c` swapped for a truncated copy)
   through a throwaway `compile_one` CMake target linked straight against
   this driver's own compiler/object/vm libraries, no full-mudlib-boot
   restart needed per attempt -- narrowed to `heart_beat()`
   (`body.c:311-327`)'s own `(--Protection[i]->time < 1)`: a prefix
   decrement whose *target* is the same member-access-via-index shape.
   Root cause: `emitIncDecExpr()`'s indexed-target branch called
   `emitExpr(*incDec.indexKey)` directly, twice, never through
   `emitIndexValue()` -- the one remaining member-access-via-index
   consumer never wired to it at all, unlike `emitExpr()`'s own
   `IndexExpr` case and every `IndexAssignExpr`/`IndexAssignStmt` site.
   Fixed by routing both re-evaluations through `emitIndexValue()`
   instead, matching the exact call shape `emitExpr()`'s own `IndexExpr`
   case already uses. This is not a static-type-resolution issue (item 3
   above already resolves what class `Protection[i]` is); it is a
   second, separate code path that never consulted that resolution.
   Named and fixed in the same turn rather than left half-done, since it
   is equally narrow (a two-line swap reusing the existing shared
   helper) and blocks the identical real statement item 3 was built for.
   The scratch `compile_one` CMake target and its scratch mudlib root
   were both removed again before this session's work was staged;
   `CMakeLists.txt` is unchanged from HEAD.

5. **Tests.** The exact `body.c:317` shape (indexed class-array element,
   member read, no cast); the same shape as a member *write*
   (`body.c:721`'s own `Protection[i]->absorb -= damage` pattern, a
   plain indexed member assignment); and confirmation that a plain
   (non-class) array's indexed element still throws the same
   `cannot resolve "->..."` error as before this row, unaffected by the
   new `IndexExpr` walk. The `--Protection[i]->time` shape itself is
   exercised live by the real `body.c:317` boot re-attempt below rather
   than a separate synthetic regression test, since it is exactly the
   real corpus statement in question.

6. **Dead Souls 3.8.2 boot re-attempted, live TCP, same method.** Both
   fixes together: `body.c` now compiles completely (confirmed directly
   too, via the same standalone `compile_one` harness item 4 used to
   bisect). Boot progresses through `body.c`, `race.c`, `combat.c`, and
   `living.c`'s own inherits, past every construct named as a blocker in
   this row's own history to date. New blocker reached, already named
   rather than newly discovered: `lib/lib/living.c` inherits
   `lib/lib/magic.c`, whose `magic.c:85`/`:96` real
   `spell->eventParse(this_object(), args...)` /
   `spell->GetTargets(this_object(), args...)` hit `Parser.cpp:1003`'s own
   pre-existing `"->: argument spread (\"...\") is not implemented yet"`
   guard (committed on HEAD at 5355afb, the same session that built the
   *array-literal* spread use, `({ exclude..., targets })`) -- a
   deliberate, clean, already-scoped-out restriction on the *call-other
   argument* spread specifically (real `expr_list_node: expr0 | expr0
   L_DOT_DOT_DOT`, the same production, used at a structurally different
   call site than the array-literal one that slice actually built), not
   a crash or a new discovery. A different, unrelated feature from this
   row's own class-array-index work; named here rather than investigated
   further, per this row's own standing "name it, don't force past it"
   discipline.

`docs/dev/ROADMAP.md` row 3.10 updated in place with this outcome.

**2026-09-04 (a further session, continuing further yet): value-producing
`comma_expr`, the named leftover on Dead Souls 3.8.2's
`lib/lib/race.c:206` `return (Race = extra), race;`.** Scoped against
real grammar first, then built as a precedence layer above `expr0`, not
as a return-only special case and not by folding comma into
`parseExpr()`. 905 tests total (903 plus 2), full suite green (direct
`build/test/amlp_tests` and `ctest` both 0 failures).

1. **Real grammar.** `comma_expr: expr0 | comma_expr ',' expr0`
   (`grammar.y:1555-1565`; 2.23 `grammar.y.pre:1545-1550`) builds
   `CREATE_TWO_VALUES` / `pop_value($1)` so the last `expr0` is the
   value. It sits *above* `expr0`, not inside it. Positions that take
   `comma_expr`: `L_RETURN comma_expr ';'` (`grammar.y:2450`),
   `statement: comma_expr ';'`, `if`/`while`/`switch`/`for_expr`,
   `'(' comma_expr ')'` (`grammar.y:2969`), `catch`, index,
   `'$' '(' comma_expr ')'`. Positions that take `expr0` via
   `expr_list_node` (`grammar.y:2488-2496`): call arguments, array
   elements. Assignment RHS is `lvalue L_ASSIGN expr0`
   (`grammar.y:1570`). So `f(a, b)` is two arguments; `f((a, b))` is one
   argument (the parenthesized `comma_expr`); `a = 1, b = 2;` as a
   statement is `(a = 1), (b = 2)`, not `a = (1, b = 2)`.

2. **This driver's prior support.** `parseExpr()` is `expr0`
   (`parseTernary()`). Statement-level and for-loop commas already
   existed (`continueStatementCommaChain` / `parseCommaExprChain`) as
   Blocks of ExprStmts that discard every value. Nothing produced a
   last-value-wins expression, so `parseReturnStatement()`'s
   `parseExpr()` then `';'` rejected the race.c form.

3. **Why not fold comma into `parseExpr()`.** That would make every
   current `parseExpr()` site a `comma_expr`, including `parseArgList`
   and assignment RHS, which would silently turn `f(a, b)` into one
   argument and `a = 1, b = 2` into `a = (1, b = 2)`. Real grammar
   forbids that. The narrow fix is a layer *above* `expr0`:
   `parseCommaExpr()` chains `parseExpr()` on comma into `CommaExpr`
   (codegen: emit left, `Pop`, emit right). Wired at every
   `comma_expr` site that previously called `parseExpr()` once (return,
   if/while/do-while/switch/for condition, parenthesized expr, catch,
   `$(...)`, FluffOS index start). `parseExpr()` stays `expr0`.
   Statement/for Blocks unchanged. LDMud `map[key, col]` still uses
   `parseExpr()` for the start so the column comma is not eaten.

4. **Tests.** The exact race.c return shape (`Race` assigned 7, returned
   value 3). Call `add(3, 4)` still two arguments; `id((8, 9))` is one
   parenthesized comma_expr yielding 9. Bare statement comma already
   covered by `testStatementLevelCommaChainAfterIndexedAndPlainAssignmentVmExecution`
   (TMI-2 `_eval.c`).

5. **Dead Souls 3.8.2 boot re-attempted, live TCP, same method.**
   `race.c` now compiles. Inherit chain reaches `lib/lib/body.c`.
   New named blocker, stopped at: `body.c:317`'s real
   `Protection[i]->time` where `Protection` is
   `private static class MagicProtection *Protection` (line 48). That
   is `->member` on an `IndexExpr` of a `class Name *` variable with
   no `(class MagicProtection)` cast, the case the class-cast slice
   explicitly left out. `staticClassTypeOf()` only knows a bare
   `VarRefExpr` or a `TypeCastExpr`. Named rather than forced.

`docs/dev/ROADMAP.md` row 3.10 updated in place with this outcome.

**2026-09-04 (a further session, continuing further): FluffOS
`(class Name)` / `(class Name *)` C-style casts, the named leftover
blocker on Dead Souls 3.8.2's `lib/lib/interactive.c` after
`object::create()` already resolved on HEAD.** Picked this over the
still-open comment-density pass on VM.cpp / CodeGen.cpp /
ParserPackage.cpp / Parser.cpp: boot is the product goal, cleanup can
wait. 903 tests total (901 plus 2), full suite green (direct
`build/test/amlp_tests` and `ctest` both 0 failures). Assessed as a
narrow extension of the existing `(string)x` strip, not a new
subsystem, so built in the same turn.

1. **Real grammar, both vendored trees agree.** `cast: '(' basic_type
   optional_star ')'` (2.9-ds2.08 `grammar.y:780-786`; 2.23-ds03
   `grammar.y.pre:839-844`). `basic_type` is `atomic_type`
   (`grammar.y:667-668`), and `atomic_type` includes `L_CLASS
   L_DEFINED_NAME` and `L_CLASS L_IDENTIFIER` (`grammar.y:632-664`;
   `grammar.y.pre:665-695`). The expression production is `cast expr0
   %prec L_NOT` (`grammar.y:2275-2279`): `$$ = $2; $$->type = $1;`, a
   compile-time type annotation with no runtime opcode. So both
   `(class marriage)Marriages[0]` and `(class marriage *)expr` are
   real casts. Real corpus: `lib/lib/interactive.c:493-494`'s
   `((class marriage)Marriages[0])->Spouse` (the extra parens are
   load-bearing: postfix `->` binds tighter than unary cast), plus
   `lib/cmds/players/biography.c:38`'s
   `(class marriage *)this_player()->GetMarriages()` and
   `secure/daemon/finger.c:129-130`'s same indexed-element form as
   interactive.c.

2. **Existing cast support could not just grow a keyword.**
   `Parser::parseUnary()` already treats `(type)` / `(type*)` as a
   no-op strip when `(` is followed by a type *Keyword* (`string`,
   `object`, ...). `class` lexes as `Ident`, so `(class marriage)`
   was never recognized and fell through as a parenthesized
   expression, then failed. Runtime stays a no-op (this driver has
   no static type checker, matching real FluffOS without
   `exact_types`). Stripping the class name the same way as
   `(string)x` is not enough when `->member` follows:
   `CodeGen::staticClassTypeOf()` only understood a bare `VarRefExpr`
   of a class-declared variable, so
   `((class marriage)Marriages[0])->Spouse` is `->` on a cast wrapping
   an `IndexExpr` and would throw `cannot resolve "->Spouse"`.

3. **What was built.** `parseUnary()` recognizes FluffOS
   `(class Name)` / `(class Name *)` (rewind if the `)` is missing, so
   a parenthesized `class` identifier is untouched). Non-star form
   wraps `TypeCastExpr { className, inner }`. Star form still strips,
   like `(string *)`, because the assignment target already carries
   the array-of-class type. `staticClassTypeOf()` returns
   `TypeCastExpr::className`; `emitExpr()` emits `inner` only. Ordinary
   `(string)x` tests unchanged (still no wrapper). Not in this slice:
   walking an `IndexExpr` of a `class Name *` variable so
   `Marriages[0]->Spouse` would work *without* a cast. No real corpus
   site does that; they all write the cast.

4. **Two new regression tests:** the exact interactive.c shape
   (`((class marriage)Marriages[0])->Spouse` reading `"Ada"`), and
   `(class marriage *)Marriages` as a runtime no-op `sizeof` matching
   `(string *)`.

5. **Dead Souls 3.8.2 boot re-attempted against `etc/driver_ds3.cfg`,
   live over a real TCP connection (Python socket client, not telnet),
   same method as before.** Master/simul_efun still boot. Sending
   `testadmin` at the installer username prompt now compiles
   `/lib/player`. `player.c:24` inherits `LIB_INTERACTIVE` *before*
   `LIB_LIVING`, and there is no compile error on
   `lib/lib/interactive.c` at all: `object::create()` (line 51) and
   the `(class marriage)` casts (lines 493-494) both compile. The
   named leftover blocker is gone.

   Boot now reaches a new, different parser gap, named here and
   stopped at rather than forced through in the same commit.
   `lib/lib/race.c:206`'s real `return (Race = extra), race;` (driver
   reported "line 222", the include-splice line-counter artifact, not
   a misread of the file). Real grammar is `L_RETURN comma_expr ';'`
   (`grammar.y:2450`; 2.23 `grammar.y.pre:2448`), and `comma_expr` is
   `expr0 | comma_expr ',' expr0` (`grammar.y:1555-1565`) building
   `CREATE_TWO_VALUES` so the *last* expr0 is the returned value.
   This driver already implements that comma operator for expression
   *statements* and for-loop init/update (`parseCommaExprChain` /
   `continueStatementCommaChain`), but those desugar to a Block of
   ExprStmts (side effect only, every value discarded).
   `parseReturnStatement()` still does `parseExpr()` then expects
   `;`, so a comma is a parse error. Reusing the statement Block as
   `ReturnStmt::expr` would drop the returned value. That is still a
   narrow follow-up (return needs last-value-wins, matching
   `CREATE_TWO_VALUES`, not a new VM opcode), but it is a different
   production from this cast work and is not built here.

`docs/dev/ROADMAP.md` row 3.10 updated in place with this outcome.

**2026-09-04 (comment-density cleanup, continuing an interrupted pass):**
inline source comments trimmed on the remaining worst-offending
`src/**/*.cpp` files, comments only, plus the type-keyword qualified
parent-call form that was already sitting unstaged from the interrupted
working tree.** Nothing was staged when this session started. The
working tree already had a partial ObjectManager.cpp comment trim (HEAD
44.0% to 34.7%) plus CLAUDE.md's new comment-density rule, mixed with
unrelated Parser/test work for grammar.y's `L_BASIC_TYPE L_COLON_COLON
identifier` form (`object::create()`). That mix is kept: the Parser
logic was not redone, only its new essay-length comments were cut to
match the same rule. 901 tests total (899 plus the two new qualified-
parent-call tests already in the unstaged tree), full suite green
(direct binary and `ctest` both 0 failures) after every file.

HEAD `src/**/*.cpp` ranked by comment percentage (the list the interrupted
session started from; headers are a different category, mostly API
docs, and were not this pass):

1. `src/scheduler/Scheduler.cpp` 47.4% (258/544)
2. `src/object/ObjectManager.cpp` 44.0% (763/1736)
3. `src/efun/EfunTable.cpp` 43.3% (5409/12492)
4. `src/main.cpp` 42.8% (80/187)
5. `src/net/Server.cpp` 37.3% (236/632)
6. `src/compiler/Lexer.cpp` 36.3% (211/582)
7. `src/vm/VM.cpp` 35.4% (1130/3193)
8. `src/compiler/CodeGen.cpp` 34.6% (615/1779)
9. `src/efun/ParserPackage.cpp` 34.3% (1091/3181)
10. `src/compiler/Parser.cpp` 33.9% (817/2408)

This session finished ObjectManager (the interrupted first pass had
stopped mid-file at 34.7%) and continued down that list through Lexer.
EfunTable.cpp was skipped: 5409 comment lines are per-efun documentation
of what each efun does, not "here is why I made this change" essays.
VM.cpp, CodeGen.cpp, and ParserPackage.cpp remain for a later pass.

Per-file comment percentage (pure `//`/`/*` lines over total lines),
HEAD to after this pass:

- ObjectManager.cpp: 44.0% (763/1736) -> 11.9% (132/1105). Interrupted
  first pass had reached 34.7% (516/1489).
- Scheduler.cpp: 47.4% (258/544) -> 17.1% (59/345)
- main.cpp: 42.8% (80/187) -> 7.8% (9/116)
- Server.cpp: 37.3% (236/632) -> 15.7% (74/470)
- Lexer.cpp: 36.3% (211/582) -> 6.3% (25/396)

Cumulative across the whole cleanup effort so far (HEAD vs now, the
five files above): 1548/3681 = 42.1% -> 299/2432 = 12.3%.

Behavioral quirks and named divergences stayed (one-line citation plus
what the code does). One accidental logic drop during ObjectManager
compression (`rebindFilename` / `loaded_` insert in
`loadVirtualObject`) was caught by
`testLoadVirtualObjectRebindsFilenameToVirtualPath` and restored before
any later file was treated as done.

The unstaged Parser work already present: `startsType()` excludes a type
keyword immediately followed by `::`, and `parsePrimary()` accepts that
shape via shared `parseQualifiedParentCall()`. That is grammar.y's
third `function_name` alternative, the Dead Souls 3.8.2
`lib/interactive.c` `object::create();` form (row 3.10 follow-up). Two
new tests cover the type-keyword qualifier among multiple inherits and
the real `autosave::Setup()` / `interface::Setup()` pattern.

**2026-09-04 (a further session, continuing further yet again): the
`class <name> { <member decls> }` struct-type declaration is built,
exactly per this session's own prior scoping report, full slice
(declaration, type usage, both construction forms, member read, member
write) together, no further staging.** 899 tests total, up from 892,
full suite green throughout (direct binary run and `ctest` both confirm
0 failures), zero regressions at any step.

1. `Value::isClassInstance` (`Value.hpp`): a plain `bool`, default
   `false`, sibling to `data`, matching real FluffOS's own T_CLASS-
   reuses-array_t shape exactly (and this session's own prior
   `Value::isUndefined`, the identical pattern) rather than a new
   `ValueVariant` alternative -- a class instance is, underneath,
   `std::shared_ptr<Array>`, the same alternative an ordinary array
   literal already uses. Set only by `OpCode::MakeArray`'s own operand
   (repurposed: 0 for an ordinary array literal, unchanged; 1 for real
   `new(class Name ...)` construction) -- no new VM opcode, matching
   this row's own scoping report exactly.
2. `MemberNameMarker` (`Ast.hpp`): the actual "no new VM opcode" reuse
   -- a placeholder AST node standing in for `IndexExpr::index`/
   `IndexAssignStmt::index`/`IndexAssignExpr::index` wherever the
   source used `instance->member` (no paren) instead of an ordinary
   index expression. `CodeGen::emitIndexValue()` (new, shared by all
   three of `emitExpr()`'s own `IndexExpr` case,
   `emitIndexAssignStmt()`, and `emitIndexAssignExpr()`, replacing
   their own direct `emitExpr(*index)` calls) recognizes it and emits a
   compile-time-resolved `PushInt` instead, then falls straight through
   to the same, completely unchanged `OpCode::Index`/`IndexAssign`
   every ordinary array index already uses. Member *write* needed no
   separate parser-level work at all: `parseStatement()`'s own pre-
   existing indexed-assignment-target lookahead (previously keyed only
   on a following `[`) was widened to also fire on `->`, and the
   expression-level `IndexAssignExpr` path already dispatches on
   whatever `dynamic_cast<IndexExpr*>` the parsed operand is -- both
   picked up member-access targets for free once `parsePostfix()`'s own
   `->identifier` (no paren) branch started building the identical
   `IndexExpr` shape the paren'd call_other branch already does.
3. Parser: `->identifier` with no following `(` is member access;
   `(` still means call_other, completely unchanged -- genuinely
   unambiguous, confirmed live (not just asserted): this driver's own
   call_other parsing already hard-required `(` immediately after, so
   a bare `->identifier` was already a guaranteed parse error before
   this row. `startsClassType()` (new, `class <identifier>` recognized
   as a type, two-token lookahead, "class" never reserved) is threaded
   through both real type-parsing paths this codebase has --
   `startsType()`/`parseTypeToken()` (already shared by
   `parseVarDeclStatement()`/`parseParamList()`/`parseForeachVar()`/
   the `for`-init clause) and `parseDeclPrefix()`'s own separate,
   pre-existing duplicate type-parsing logic (top-level function/
   object-variable declarations) -- confirmed by reading both directly
   rather than assuming one covers the other. `new(class Name field:
   val, ...)` recognized narrowly (`name == "new" && checkText("(") &&
   peekAt(1).text == "class"`), leaving a real, unrelated, unimplemented
   `new("/obj/file")` clone-style call (a different real grammar
   production, out of this row's own scope) to fall through to the
   ordinary named-call path exactly as before. `ForeachVarSpec`/
   `ForeachStmt` gained a `classType` field (previously discarded
   entirely) for the real `foreach(string svc, class service s in
   Services)` shape.
4. Compile-time-only member resolution, matching real
   `reorder_class_values()`/`lookup_class_member()` exactly: `classDefs_`
   (class name to ordered member names, real `class_def_t` analog) and
   `localClassTypes_`/`objectVarClassTypes_` (declared variable to its
   class name, block-scoped for locals via the same `localScopeStack_`/
   `emitBlock()` cleanup `locals_` itself already uses) are the only new
   CodeGen-side state. `staticClassTypeOf()` only ever resolves a bare
   declared-class-typed variable (`VarRefExpr`); anything else --
   confirmed live by a dedicated regression test, not just designed in
   the abstract -- throws a clear "cannot resolve ... static class type
   is not known here" compile error rather than replicating real
   FluffOS's own documented ambiguity-prone `TYPE_ANY` fallback
   (`lookup_any_class_member()`), matching this codebase's established
   "throw rather than silently mishandle" convention; no real corpus
   site needs anything richer (confirmed in the prior scoping session,
   re-confirmed here by this exact test).
5. Partial-field construction matches real `reorder_class_values()`
   (`compiler.c:417-465`) exactly: every omitted field becomes a plain
   `PushInt 0`, never `isUndefined`/monostate -- confirmed by a
   regression test checking the omitted-field value specifically, not
   just that construction succeeds.
6. `classp()` (real `efuns_main.c`'s `f_classp()`) was already
   registered as an always-false stub (this driver had no class
   representation at all yet); now checks
   `holds_alternative<shared_ptr<Array>> && isClassInstance` together,
   matching real `sp->type == T_CLASS` exactly. Every ordinary array-
   oriented efun/opcode needed no changes at all (confirmed by reading,
   not assumed): `isTruthy()`/`valuesEqual()`/`sizeof()`/`foreach`/
   arithmetic array concatenation all key only on `data`'s own
   `shared_ptr<Array>` alternative, exactly like real FluffOS's own
   ordinary array opcodes, which do not special-case `T_CLASS` either.
7. FluffOS-dialect-only throughout, confirmed a clean parse error under
   LDMud, not a silent misbehavior (a dedicated regression test, not
   just reasoned about): every new recognition point
   (`startsClassType()`, the top-level class-declaration lookahead,
   `new(class ...)`, the no-paren `->member` branch) gates on
   `dialect_ == LpcDialect::FluffOS`. Under LDMud, `class death { int
   Date; }` parses exactly as it would have before this row existed --
   `class` consumed as an ordinary declaration's own name
   (`parseDeclPrefix()`'s "type omitted" branch), then failing with the
   literal same real `"expected \";\" ... (got \"death\")"` error the
   original, pre-this-row Dead Souls boot blocker itself produced.
8. 7 new regression tests: declaration + full-field-list construction +
   partial-field-list construction (checking the omitted-field value
   specifically) + member read together; a `mixed`-declared variable's
   member access throwing rather than guessing; the exact real
   `secure/daemon/inet.c` idiom (class-typed variable, empty
   construction, member write, member read); `classp()` distinguishing
   a class instance from a plain array and a number; the real
   `daemon/soul.c` shape (a class instance holding a mapping-typed
   member of class instances, retrieved through a member-then-index
   chain into a class-typed local); the real `secure/daemon/inet.c`
   class-typed `foreach` value variable; and the LDMud clean-error case.
9. **Dead Souls 3.8.2 boot re-attempted against `etc/driver_ds3.cfg`,
   live over a real TCP connection, same method as before.** Confirmed
   real progress: `lib/lib/include/player.h`'s own `class death { int
   Date; string Enemy; }` no longer blocks compilation at all --
   `player.c` (which includes it) now compiles further than before and
   reaches a different file entirely, one of its own real `inherit`
   targets. Boot now reaches a new, different, previously-undocumented
   blocker: `lib/lib/interactive.c:115`'s real `autosave::Setup();` --
   real LPC's explicit *named*-inherited-parent call syntax
   (`<ParentName>::function(args)`, distinct from the bare `::function()`
   parent-call form this driver already implements), needed here because
   `interactive.c` multiply-inherits and this specific call must reach
   one particular parent's own `Setup()` by name. This driver's parser
   currently misreads `autosave` as an attempted declaration's own type
   and chokes on the following `::` ("parse error: expected token type
   in variable declaration name at line 116 (got \"::\")" -- the real
   file's own line 115 one-indexed off by the parser's own line-
   counting convention, not misattributed). Named here and stopped at,
   not investigated further or forced past, per this project's own
   standing "no half-built subsystems" discipline -- scoping named-
   inherited-parent calls is its own, separate future session's work.
   `docs/dev/ROADMAP.md` row 3.10 updated in place with this outcome.

**2026-09-04 (a further session, continuing further still): CLAUDE.md's
Orientation section updated with the second vendored-tree pointer
(approved, applied directly), and the `class <name> { <member decls> }`
struct-type declaration blocking Dead Souls 3.8.2's boot scoped in full
against real FluffOS source and this driver's own current code, no code
written yet.** CLAUDE.md gained one new Orientation paragraph, right
after the existing `temp/reference/fluffos-2.9-ds2.08/` one, naming
`temp/ds3.8.2_extracted/ds3.8.2/fluffos-2.23-ds03/` as a second real,
on-disk, gitignored FluffOS tree narrower in scope (Dead Souls 3.8.2's
own bundled driver version specifically) than the canonical one.

1. **Real grammar, both vendored trees agree (same productions, close
   line numbers):** declaration is `type_decl: type_modifier_list
   L_CLASS identifier '{' member_list '}'` (2.9-ds2.08 `grammar.y:
   532-560`; 2.23-ds03 `grammar.y.pre:568-`), no trailing `;` after
   `}` -- matches `player.h`'s own exact shape. `member_list:
   member_list basic_type member_name_list ';'` (`grammar.y:522-530`)
   allows any `basic_type`, including a nested `class <name>` (see
   item 6). Construction is `new(class Name field: val, field: val,
   ...)` -- **not** a positional `(class name val, val, ...)` literal
   -- production `L_NEW '(' L_CLASS L_DEFINED_NAME opt_class_init ')'`
   (`grammar.y:3383-3418`; identical shape at `grammar.y.pre:3391-`),
   built from `class_init: identifier ':' expr0` /
   `opt_class_init: /*empty*/ | opt_class_init ',' class_init`
   (`grammar.y:3327-3349`). `class <name>` is also a real usable
   *type* anywhere `atomic_type` appears (`atomic_type: L_BASIC_TYPE |
   L_CLASS L_DEFINED_NAME | L_CLASS L_IDENTIFIER`, `grammar.y.pre:
   665-695`) -- variable/parameter/return-type declarations, not just
   construction.
2. **Runtime representation, confirmed directly against `class.c`/
   `interpret.c`, not assumed:** `T_CLASS` (`lpc.h:66`, `0x200`) is a
   real, distinct top-level svalue *type tag* -- but its storage is a
   plain, ordinary `array_t*` reused wholesale (`push_class()`/
   `push_refed_class()`, `interpret.c:1301-1316`: `sp->type = T_CLASS;
   sp->u.arr = v;`, the identical union field an ordinary array uses).
   `allocate_class()` (`class.c:20-37`) fills an empty instance
   (`new(class Name)`, no field initializers) with plain `const0` per
   member -- **not** `const0u`/`T_UNDEFINED` -- so a freshly
   constructed empty class instance's members read `undefinedp()`
   false, unlike an ordinary declared-but-unassigned local/object
   variable (this session's own prior T_UNDEFINED work). A partial
   field list (`new(class Name field1: val1)`, some members omitted)
   is resolved entirely at *compile time*: `reorder_class_values()`
   (`compiler.c:417-465`) synthesizes a literal `0` constant for every
   omitted member, so by the time codegen runs, construction is always
   "push exactly `cd->size` values in declared-member order" -- no
   partial-field logic exists at runtime at all.
3. **Member access is `->`, not dot notation** (correcting this
   session's own initial framing): real FluffOS reuses the *same*
   `L_ARROW` token as call_other, but as two entirely separate, non-
   conflicting grammar productions disambiguated purely by whether a
   `(` follows -- `expr4 L_ARROW identifier` (no paren, `grammar.y:
   2803-2827`, member access, `F_MEMBER`/`F_MEMBER_LVALUE`) versus
   `expr4 L_ARROW identifier '('` (paren required, `grammar.y:3580`,
   call_other). `F_MEMBER` (`interpret.c:3070-3087`) is a plain
   `arr->item[i]` read where `i` is a *compile-time-resolved* member
   index (`lookup_class_member()`, an `EXTRACT_UCHAR(pc++)` byte
   operand baked in by the compiler, never resolved at runtime);
   `F_MEMBER_LVALUE` (`interpret.c:3088-3105`) is the identical shape
   producing an lvalue for a member *write* (`instance->member =
   value`). A class instance is by-reference on assignment (it is a
   refcounted `array_t*`, `push_refed_class()`/`free_class()`, exactly
   like an ordinary array). `save_object()`/`save_svalue()`
   (`object.c`, confirmed by the same subtype/type-only switch this
   session's T_UNDEFINED work already read) has no `T_CLASS` case at
   all -- a class instance cannot be saved, matching this driver's own
   real "not implemented" convention for other unsaveable kinds rather
   than a silent misrepresentation.
4. **This driver's own scope, and a significant reuse opportunity
   confirmed, not assumed:** entirely new AST/CodeGen/VM surface --
   no `classDef`/`ClassDecl`/`structDef` symbol exists anywhere in
   `include/`/`src/compiler/`/`src/vm/` today (checked directly), and
   `class` is not yet a lexed keyword (checked `Lexer.cpp` directly --
   lexes as a plain identifier now, so needs the same contextual,
   non-blanket-reservation discipline row 3.10's own earlier
   `ARRAY_RESERVED_WORD` fix already established, not a hard keyword
   that could misread a real file's own unrelated `class`-named
   identifier). Confirmed this driver's *own* `Array`
   (`std::vector<Value> items`) and `OpCode::Index`/`IndexAssign` (its
   existing array-indexing opcodes) already provide everything real
   `F_MEMBER`/`F_MEMBER_LVALUE` need: since real member-index
   resolution is 100% compile-time (item 2), `instance->member` and
   `instance->member = value` can desugar directly to this driver's
   *existing* `Index`/`IndexAssign` opcodes with a compile-time-
   resolved constant index, needing no new VM opcode at all -- the new
   AST nodes are a `ClassDeclStmt` (recorded into a new CodeGen-side
   compile-time symbol table, class name to ordered member-name list,
   the direct analog of real `class_def_t`/`class_member_entry_t`) and
   a `NewClassExpr` (desugars named/partial field initializers into
   positional order at compile time, mirroring real
   `reorder_class_values()` exactly, then emits an ordinary
   `MakeArray`-shaped sequence). Real corpus evidence closes an open
   question: `classp()` (real `func_spec.c:181`/`182`, both trees) is
   live in this exact corpus (`secure/sefun/identify.c`,
   `secure/lib/net/client.c`, `daemon/intermud.c`), so a class instance
   cannot be *indistinguishable* from a plain array at the Value level
   the way a naive "just reuse Array with zero marking" reading might
   suggest -- real FluffOS itself only needs a single is-a-class-
   instance bit for this (`f_classp()`, `efuns_main.c:409-418`, checks
   `sp->type == T_CLASS` only, no per-class identity), so the natural
   fix is one small marker on `Array` (an `isClassInstance` bool, or
   `optional<string> className` if per-class `classp()`-style identity
   is ever needed beyond real FluffOS's own coarser bool), the same
   sibling-flag shape this session's own `Value::isUndefined` already
   established as this codebase's working pattern for "type stays the
   same, one bit distinguishes it" -- not a new `ValueVariant`
   alternative.
5. **Smallest real first slice: the user's own proposed split
   (declaration + construction + member read, before member write) is
   not real, corrected by direct corpus evidence, not assumed.**
   `secure/daemon/inet.c:76-83` constructs an *empty* instance
   (`new(class service)`, no field initializers) then writes three
   members via `->` assignment before ever reading any of them
   (`s->PortOffset = port_offset; s->SocketClass = socket_class;
   s->SocketType = type;`), a real, load-bearing "construct empty,
   fill in fields one at a time" idiom distinct from the named-
   initializer construction form -- so member *write* is not a safely
   deferrable edge case, it is required by the same file that needs
   empty construction at all. The real minimal complete slice is:
   declaration, `class <name>` as a usable type (variable/parameter/
   return-type positions), both construction forms (named-field and
   empty), member read, and member write, together -- `classp()`
   (item 4) and array-literal-compatibility/`foreach`-over-a-class-
   valued-mapping (item 6) are the genuinely deferrable edges, not
   member write.
6. **Corpus evidence: 21 real files in Dead Souls 3.8.2 declare a
   `class` type, 15 real files (2 more are doc pages, not code)
   construct one via `new(class ...)`.** `player.h`'s own `class death
   { int Date; string Enemy; }` is representative of most
   *declaration* shapes (`daemon/include/classes.h`'s `Skill`/`Class`,
   `daemon/include/party.h`, etc. -- plain basic-type members, 1-3
   members, no nesting) but is not representative of real *usage*
   depth: `player.c` itself never actually constructs or accesses a
   `class death` value anywhere (`Deaths` is a plain array-of-mappings
   instead, confirmed by reading `player.c` directly -- `class death`
   is vestigial header scaffolding for this one file specifically), so
   the bare declaration grammar alone is sufficient to clear *this
   specific* boot blocker. `daemon/soul.c` (the emote/social-command
   system) and `secure/daemon/inet.c` (the service/socket daemon) are
   real, heavier, more representative consumers found in the same
   corpus: class-typed local-variable declarations (`class emote e;`,
   `class rule r;`, `class service s;`), member-chained access
   (`e->Rules[rle]`), class instances stored as mapping values with a
   class-typed `foreach` loop variable
   (`foreach(string svc, class service s in Services)`,
   `secure/daemon/inet.c:164`), and the empty-construct-then-write
   idiom (item 5). Both files are real modules this mudlib loads
   during ordinary operation, not deep edge-case content -- building
   only the declaration (the minimum to pass the immediate blocker)
   would just relocate the real blocker to one of these two files
   shortly after, not resolve the feature.

Scoped as a FluffOS-dialect feature specifically, matching
`etc/driver_ds3.cfg`'s own `dialect: fluffos`; real LDMud has an
analogous but not necessarily identical `struct` construct, out of
scope for this investigation and not read here.

Not yet implemented; waiting on explicit go-ahead before writing code.

**2026-09-04 (a further session, continuing further): T_UNDEFINED gap
built exactly per this session's own prior scoping report below, and
Dead Souls 3.8.2's boot re-attempted past the installer's `privs_file()`
check.** 892 tests total, up from 888, full suite green throughout
(direct binary run and `ctest` both confirm 0 failures), zero
regressions at any step.

1. `Value::isUndefined` (`Value.hpp`): a plain `bool`, default `false`,
   sibling to `data`, matching real FluffOS's own `type`+`subtype`
   svalue shape exactly rather than a new `ValueVariant` alternative --
   no `std::visit`/exhaustive-switch site exists over the variant
   (confirmed, not just carried over from the scoping read), so this
   needed no changes anywhere arithmetic/comparison/truthiness already
   read `data`. `makeUndefinedNumber()` is the one shared constructor
   for real const0u (`Value(int64_t{0})` with `isUndefined = true`),
   used at all three real default-init call sites so there is exactly
   one place that builds the real shape.
2. Dialect-conditional at all three sites, exactly as scoped: `VM.cpp`'s
   `run()` and `runAsync()` (two separate locals-init call sites) gate
   on `config().dialect() == "fluffos"` directly; `LpcObject`'s
   constructor gained a third `bool fluffosDialect = true` parameter
   (default matches `Config::dialect()`'s own default), threaded from
   both real `ObjectManager::loadObject()`/`cloneObject()` call sites
   via `config_.dialect() == "fluffos"`. Real LDMud gets a plain,
   untagged 0 at every one of these sites, matching its own genuine
   lack of the concept (confirmed against `temp/ldmud/src/svalue.h`
   directly in the scoping pass below, not re-guessed here).
3. `undefinedp()`/`nullp()` (`EfunTable.cpp`, real aliases of one
   function, unchanged) now check `holds_alternative<monostate> ||
   args[0].isUndefined` instead of monostate alone -- both real cases
   preserved: a missing mapping key (monostate, already correct,
   unaffected) and a genuinely never-assigned FluffOS-dialect local or
   object variable (the new flag). The stale comment there (predating
   the object-var/local default-value fix, still describing monostate
   as "currently what an object variable reads as") is corrected in
   place, not left stale next to the fix. A second, same-vintage stale
   comment found in passing, `test_lexer.cpp`'s own
   `testUndefinedpTrueOnlyForVoidNotZeroOrOtherTypes`, corrected too.
4. Re-verified against the actual new field, not just the scoping
   pass's source-only read: `isTruthy()`/`valuesEqual()` (`Value.cpp`)
   read only `data.index()`/the stored numeric value, never
   `isUndefined` -- confirmed by reading both functions directly.
   `serializeValue()` (`EfunTable.cpp`'s `save_object()` backing)
   writes `'I' << *iv << ';'` for any `int64_t`, `isUndefined` never
   read -- an undefined 0 saves and restores as a plain defined 0,
   matching real FluffOS's own subtype-blind `save_svalue()`.
   `sprintfNumericArg()` accepts any `int64_t` unconditionally,
   `isUndefined` never read -- matches real sprintf.c's own type-only
   check. Every arithmetic opcode (`Sub`/`Mul`/`Div`/`Mod`/`Add`, read
   directly in `VM.cpp`) constructs a fresh `Value(int64_t{...})` for
   its result, so `isUndefined` resets to its `false` default for free
   on every computed value, matching real FluffOS's own explicit
   `subtype = 0` reset on every arithmetic opcode without needing an
   explicit reset here. `++x`/`--x` has no dedicated opcode at all
   (desugars to the same `Add`/`Store` path) so needed no separate
   check. `StoreLocal`/`StoreObjectVar` do a plain whole-`Value` copy,
   correctly propagating `isUndefined` on a bare `x = y` the same way
   real `assign_svalue()`'s whole-`svalue_t` copy does (`interpret.c:
   617-622`, confirmed directly) -- so assigning one already-undefined
   variable into another also reads undefined, matching real behavior,
   not just "reads as 0".
5. 4 new regression tests, `test_lexer.cpp`: a never-assigned local
   reading `undefinedp()`/`nullp()` true under FluffOS and false under
   LDMud from the identical source; an explicitly-assigned `0` local
   reading false under both dialects; one broad arithmetic/comparison/
   truthiness/string-concat test running (not just asserting) `u + 1`,
   `1 - u`, `u * 5`, `u == plain`, `u == 0`, `plain == u`, `u != 1`,
   `u < 1`, `u > -1`, `u ? 1 : 0`, `!u`, and `"n=" + u` against an
   undefined local, all matching plain-0 results exactly; and the
   object-variable sibling case (LDMud false, FluffOS true) mirroring
   `LpcObject.cpp`'s own default-init site specifically, not just
   locals.
6. **Dead Souls 3.8.2 boot re-attempted against `etc/driver_ds3.cfg`,
   live over a real TCP connection (Python client script), same method
   prior sessions used.** Confirmed real progress past the exact named
   blocker: the installer's `"What is your MUD admin username?"` prompt
   now accepts `testadmin` and proceeds into real account-creation
   (attempting to `clone_object()` `/lib/player` for the new save file)
   with no privilege-denial reached anywhere in the
   `BANISH_D`/`check_access()`/`privs_file()` chain -- the previous
   session's own hard "no privs assigned" denial is gone. Boot now
   reaches a new, different, previously-undocumented real parser gap
   (not found or named in any earlier session): `lib/lib/include/
   player.h:4`'s real `class death { int Date; string Enemy; }` --
   real FluffOS's own `class <name> { <member decls> }` struct-type
   declaration, reached via `player.c`'s own `#include "include/
   player.h"`. This driver's parser has no `class`-type-declaration
   grammar at all; seeing `class` where it expects a variable
   declaration's own base type, it consumes `class` itself as the
   variable name and then chokes on the next real token, `death`
   (surfacing as "expected \";\" in object variable declaration ...
   got \"death\"" -- the driver's own line-number report, "line 1", is
   an artifact of the include-splice rewrite resetting the line
   counter, not literally `player.h`'s own line 1, confirmed by reading
   `player.h` directly and finding `class death` at its real line 4).
   Named here and stopped at, not investigated further or forced past,
   per this project's own standing "no half-built subsystems"/"name a
   real blocker, don't force past it" discipline -- scoping a `class`
   type declaration is its own, separate future session's work.
   `docs/dev/ROADMAP.md` row 3.10 updated in place with this outcome.

Documentation-structure flag, no action taken (per explicit instruction
to wait for go-ahead, this is not a code/scope decision): CLAUDE.md's
own Orientation section documents only `temp/reference/
fluffos-2.9-ds2.08/` as "the vendored real FluffOS 2.9 source used for
every citation throughout this repo"; it does not mention the second
real vendored tree this and the prior session both cited directly,
`temp/ds3.8.2_extracted/ds3.8.2/fluffos-2.23-ds03/` (the exact driver
version Dead Souls 3.8.2 itself ships with, bundled inside that
mudlib's own extracted archive rather than placed under `temp/
reference/`). This gap caused a real wrong claim in the prior session
("does not exist anywhere on disk"), corrected once actually checked.
Proposed one-line addition, pending go-ahead: a new paragraph in
CLAUDE.md's Orientation section, immediately after the existing
`temp/reference/fluffos-2.9-ds2.08/` paragraph (after line 56, before
the section's closing), naming `temp/ds3.8.2_extracted/ds3.8.2/
fluffos-2.23-ds03/` as a second real, on-disk, gitignored FluffOS
source tree, narrower in scope than the canonical one (citations tied
specifically to Dead Souls 3.8.2's own bundled driver version rather
than general cross-repo citations), so a missing-file check against
only the canonical path does not wrongly conclude a real, on-disk
citation target does not exist.

**2026-09-04 (a further session): T_UNDEFINED gap scoped in full against
real FluffOS/LDMud source and this driver's own current code, no code
written yet, per this project's own "scope first" discipline.** Also
closed out this session's own opening verification task: STATUS.md's
row-3.10 entry already read "888 tests total, up from 874" and "14 new
regression tests" (confirmed against a direct `ctest`/binary run: 888
test cases, 0 failed, `all tests passed`); no 995/18 figure was present
in the file, so nothing needed correcting there.

1. **Real FluffOS representation, confirmed directly against source
   (both vendored trees agree):** `T_UNDEFINED` (`lpc.h:89`, value
   `0x4`) is a *subtype* value, meaningful only when a `svalue_t`'s
   `type` field is `T_NUMBER` -- not a distinct top-level type tag.
   `main.c:120-122` (`temp/reference/fluffos-2.9-ds2.08`) /
   `main.c:123-125` (`temp/ds3.8.2_extracted/ds3.8.2/fluffos-2.23-ds03`,
   the exact driver version Dead Souls 3.8.2 ships with, a second real
   vendored tree on disk not currently named in CLAUDE.md's Orientation
   section) both construct `const0u` identically: `type = T_NUMBER`,
   `u.number = 0`, `subtype = T_UNDEFINED` -- bit-for-bit the same as a
   literal `0` except that one field. `interpret.c`'s own
   `setup_variables()` (`push_undefineds(local)`, `:1386-1390`) confirms
   every declared local, not just object variables, defaults to
   `const0u` on every function call; `object.c:1595/1892/1950` and
   `simulate.c:988` confirm the same for object variables;
   `mapping.c:827-847`'s `find_in_mapping()` confirms a missing mapping
   key also returns `const0u`, not a plain `const0`. Only
   `undefinedp()`/`nullp()` (`efuns_main.c:3456-3466`, confirmed real
   aliases of the same function via `efun_defs.c:141`'s
   `F_UNDEFINEDP | F_ALIAS_FLAG`) observe the subtype; `==` (`f_eq()`,
   `eoperators.c:97-107`) compares only `u.number`, and every arithmetic/
   assignment opcode explicitly resets `subtype = 0` on its result
   (confirmed by grep across `eoperators.c`/`interpret.c`), so the
   distinction never survives being used in an expression. `sprintf()`'s
   own `%d`/`%o`/`%x`/`%c` type check (`sprintf.c`, cited already in
   `EfunTable.cpp:3106-3120`) and `save_svalue()`/`save_object()`
   (`object.c:138-`, switches on `type` only) both ignore `subtype`
   entirely -- a saved-and-restored undefined value comes back as a
   plain defined `0`, matching this driver's own existing sprintf
   comment.
2. **LDMud has no equivalent concept at all**, confirmed directly against
   `temp/ldmud/src/svalue.h`: no `T_UNDEFINED`, no `undefinedp`/`nullp`
   efun anywhere in source or `doc/`, and the secondary-type union on
   `struct svalue_s` (`x.exponent`/`x.closure_type`/`x.quotes`/etc.) has
   no "was this ever assigned" slot. `F_CLEAR_LOCALS`
   (`interpret.c:16269-16284`) resets locals to plain `const0`, not any
   undefined-tagged variant. This is a real, permanent, FluffOS-only
   distinction, not a gap in LDMud's own source -- any fix stays
   dialect-conditional (a no-op under LDMud) rather than universal.
3. **This driver's own `Value` type and default-init sites, checked
   directly, not guessed:** `VM.cpp:1533` (`VM::run()`) and `VM.cpp:2992`
   (`VM::runAsync()`, a second, parallel call-setup site) both default
   `locals` to `Value(int64_t{0})`; `LpcObject.cpp:29` does the same for
   `variables_`. Both carry comments explicitly framing this as a known
   simplification over a prior, arithmetic-breaking `std::monostate`
   default. `undefinedp()`/`nullp()` (`EfunTable.cpp:4182-4187`) already
   exist and correctly check `std::holds_alternative<std::monostate>`,
   but that comment (`:4176-4179`) is now stale: it still says monostate
   is "currently what an object variable reads as," which stopped being
   true the moment `LpcObject.cpp`/`VM.cpp` were changed to a real `0` --
   so today `undefinedp()` can never see a never-assigned local or
   object variable at all, only a missing-mapping-key monostate (which
   remains correctly detected, matching real `find_in_mapping()`).
   `CodeGen.cpp:721-727`'s `emitVarDeclStmt()` confirms it emits no
   bytecode at all for a declared-without-initializer local, relying
   entirely on the VM's default fill -- so there is no third site to
   find there. `isTruthy()`/`valuesEqual()` (`Value.cpp`) key only off
   `data.index()` and the stored numeric value, matching real `f_eq()`'s
   subtype-blind comparison already -- neither needs to change. The
   natural fix shape (matching real FluffOS's own `type`+`subtype`
   struct exactly, not a new variant alternative): a plain
   `bool isUndefined = false;` sibling field on `struct Value` next to
   `data`, set `true` only at the two default-init sites (plus
   `runAsync`'s copy, three call sites total), left at its default
   everywhere a fresh `Value(int64_t{...})` is constructed by arithmetic
   or a literal -- which every such site already does today, so no
   opcode needs to change. Real FluffOS's `assign_svalue()` (`:617-622`)
   copies the whole `svalue_t` including `subtype` on a plain `x = y`,
   so a bare bool field (copied automatically by `Value`'s default copy
   constructor) matches that propagation for free; nothing here needs a
   `std::visit`/exhaustive-switch update since `Value.hpp:54-60`'s own
   comment already confirms none exist over the variant.
4. **Smallest real first slice: yes, genuinely just those two efuns,
   confirmed rather than assumed.** Checked the two real named
   candidates for a wider distinction directly: `sprintf()`'s `%d`-family
   type check and `save_object()`'s `save_svalue()` both switch on
   `type`/`data.index()` only, never `subtype` (source cited under item
   1) -- so real FluffOS itself does not distinguish the two anywhere
   outside `undefinedp()`/`nullp()`. The complete slice: add the
   `isUndefined` flag, set it at the three default-init call sites,
   and repoint `undefinedp()`/`nullp()` at
   `(isUndefined && holds int64_t 0) || holds monostate` instead of
   monostate alone (both must keep working: the mapping-miss case is
   real and already correct). Nothing else needs to change.
5. **Confirmed this fully unblocks the named `privs_file()` case, and
   nothing further is needed for it.** Real source:
   `temp/ds3.8.2_extracted/ds3.8.2/lib/secure/daemon/master.c:214-236`.
   `mixed ret;` is a declared-only local; the only path that leaves it
   untouched is `nom` being falsy, at which point real FluffOS's `ret`
   genuinely never receives an assignment before `if(undefinedp(ret))`
   -- exactly the never-assigned-local case, not an object variable or a
   mapping miss. `file_privs()` itself
   (`temp/ds3.8.2_extracted/ds3.8.2/lib/secure/sefun/security.c:9-30`)
   is a plain per-directory string lookup (`PRIV_MUDLIB` for `daemon`,
   `PRIV_SECURE` for `secure`/`adm`, etc.), and every downstream consumer
   of `privs_file()`'s return (`check_access()`'s own
   `query_privs()`/`member_array()` calls, `master.c:353/511/520-521/
   574-578`) is ordinary string/array logic untouched by the undefined
   distinction. So fixing the local-variable default plus
   `undefinedp()` is sufficient end to end for this call site: no
   further change anywhere in the master.c/`check_access()` chain is
   needed.

Not yet implemented; waiting on explicit go-ahead before writing code.

Dated session entries below, most recent first. `STATUS-ARCHIVE.md`
(which used to hold everything before the 5 most recent sessions) was
deleted 2026-08-21 by the project owner directly (`git log`: "deletion
of archieved documents", a real human commit, not an assistant action);
noted here rather than left as a dangling reference, since this file's
own header used to point at it. This file no longer trims itself to a
fixed recent-session count now that there is nowhere to move older
entries to -- it is expected to keep growing.

**2026-09-04 (continuing the same session, after the false-premise
report below): row 3.10's own named blocker (real LPC's `...`
array/call spread operator) is built, and Dead Souls 3.8.2 fully boots
for the first time.** Implemented exactly per this session's own prior
scoping report (real `grammar.y:706-717`/`2488-2496`, real
`interpret.c:1394-1410`/`2680-2724`, real `icode.c:250-275`), full
citations already in `Ast.hpp`/`Bytecode.hpp`/`VM.cpp`/`CodeGen.cpp`'s
own comments, not repeated here:

1. The real prerequisite, varargs rest-parameter capture
   (`FunctionDecl::isVarargs`/`AnonFunctionExpr::isVarargs`): the last
   declared parameter of a `mixed args...`-style function now really
   collects the caller's remaining arguments into an array (real empty
   array on too few arguments, not undefined/0), where
   `Parser::parseParamList()` previously parsed and silently discarded
   the trailing `"..."`.
2. The spread primitive itself: one new `OpCode::ExpandVarargs`
   (splices an array into one stack slot in place, accumulates a size
   delta) plus a small `pendingVarargsDelta` local in `VM::run()` (a
   deliberate, cited deviation from real FluffOS's own global
   `num_varargs`: this driver recurses through nested `VM::run()` calls
   with a fresh operand stack each time, so a local gives the identical
   behavior with no cross-call leakage risk). `MakeArray`, `MakeMapping`,
   `Call`, `CallParent`, and `CallEfun` each consume and reset it.
3. Parser/AST wiring: `CallExpr::argIsSpread`/
   `ArrayLiteralExpr::elementIsSpread` (parallel `vector<bool>`, empty
   by default), trailing `"..."` handled in `parseArgList()` (shared by
   plain calls/`::name()`/`efun::name()`/`(*fp)(...)`) and the array-
   literal loop. `CallOtherExpr` (`->`/`call_other()`) and `sscanf()`
   deliberately reject a spread argument outright rather than silently
   mishandling it -- named, not implemented this slice.
4. 11 new regression tests: rest-parameter capture (zero/one/many extra
   arguments), array-literal spread (empty/single/multi-element arrays,
   checking contents and position, not just count), call-argument
   spread through `Call` and `CallEfun` live, `CallParent`'s own
   emission checked directly (its own consumption is the mechanically
   identical one-line pattern `Call`/`CallEfun` already prove live; a
   real multi-file inherit chain to exercise it live was judged out of
   proportion to what that one shared line needs re-proving), and the
   exact `secure/sefun/sefun.c` shape end to end (a captured rest-
   parameter immediately re-spread into a forwarded call).

Re-attempting the Dead Souls boot immediately surfaced a real, general,
pre-existing parser gap in the *same family* as row 3.10's own earlier
`ARRAY_RESERVED_WORD` fix, not caused by the spread work: real
`grammar.y.pre:697-715`'s own `basic_type: atomic_type |
opt_atomic_type L_ARRAY` makes `opt_atomic_type` itself optional (real
"/* empty */" alternative, defaulting to `TYPE_ANY`/"mixed"), so a
completely bare `array` (no preceding base type at all, e.g.
`secure/sefun/sockets.c`'s own real `foreach (array item in
finalsocks)`) is real LPC too -- the earlier fix only ever covered the
two-word `"<type> array"` form. Real corpus confirms all four
declaration positions: `lib/guard.c`'s own object variable, `lib/std/
story.c`'s own local variable and return type, `lib/std/bane.c`'s own
parameter. The object-variable/return-type case was the most dangerous
of the four: `Parser::parseDeclPrefix()`'s own "type omitted entirely"
fallback silently consumed a bare `array` as the declaration's own
*name* instead of failing loudly. Fixed with two new shared helpers,
`Parser::startsType()`/`parseTypeToken()`, used at all six real
positions (statement dispatch, `for`-init, `parseParamList()`,
`parseDeclPrefix()`, `parseForeachVar()`, and `parseSingleVarDecl()`'s
own three call sites, the last of which needed its own signature change
to `(typeText, isArray)` since a comma-separated declaration's own
per-name array marker -- real `"mixed *privs, *ok;"`, already covered
by `testBitAndVmExecutionOnArraysIsIntersection` -- has to be
re-checked independently for each name, a real regression this session
introduced and caught itself before landing, not shipped). One new
regression test, `testBareArrayKeywordWithNoPrecedingTypeWorksInEvery
PositionIncludingForeach`, covering all four declaration positions plus
the foreach case that actually blocked the boot.

Continuing the boot found two more real, narrow gaps, same rigor:

- `get_config()` missing real index 29 (`__MAX_STRING_LENGTH__` =
  `CFG_INT(14)`, `14 + BASE_CONFIG_INT(15) = 29`), needed by
  `secure/sefun/sefun.c`'s own `read_file()` wrapper, itself called
  from a top-level object-variable initializer in the same file --
  blocked the simul_efun object from loading at all. New
  `Config::maxStringLength()` (default and `etc/driver_ds3.cfg`'s own
  new `max_string_length: 200000` line both taken directly from this
  mudlib's own real bundled `bin/mudos.cfg`, not guessed).
- `get_garbage()` (real `packages/contrib.c`'s own `f_get_garbage()`,
  on in this exact bundled build), needed by `secure/sefun/sefun.c`'s
  own `call_out()` wrapper, called unconditionally on every real
  `call_out()`. Real `garbage_check()`'s exact four-part condition
  ported directly using infrastructure that already existed
  (`LiveObjectRegistry::all()`, `isClone()`, `environment()`,
  `shadowing()`): a real clone, no environment, not shadowing, and
  real `ob->ref == 1`. That fourth condition is real code but
  confirmed live (while writing its own regression test) to be
  effectively always-empty in this driver specifically: real
  FluffOS's own object table is itself a strong holder, so a real
  `ref==1` object stays resident until something later finds and
  destructs it, while `LiveObjectRegistry` deliberately holds only
  `weak_ptr` (so ordinary C++ RAII, not a manual sweep, frees an
  object once nothing references it) -- the instant an object here
  would satisfy real `ref==1`, its one remaining strong reference is
  the very statement that would have made it eligible, and it is freed
  by that statement before any later `get_garbage()` call could
  observe it. Named plainly in `EfunTable.cpp`'s own comment as a real
  lifecycle-model difference, not a bug to chase further; the other
  three conditions are real and tested.

**Dead Souls 3.8.2 boots fully for the first time: "Driver booted.
Master object loaded", listening, accepting connections.** Verified
live over a real TCP connection to the real installed mudlib. Two more
real, narrow bugs found and fixed reaching the installer's first real
prompt:

- `input_to()` only ever accepted a string function name, rejecting a
  closure/function-pointer first argument outright -- real
  `simulate.c`'s own `input_to()` accepts either, and Dead Souls' own
  real installer (`secure/lib/connect.first.c`) registers every one of
  its prompts as a closure (`input_to((: InputName :), I_NOESC);`),
  breaking new-connection logon entirely for this mudlib. Fixed by
  widening `PendingInputTo::function` from a plain `std::string` to a
  string|function `Value` (the same shape `notify_fail()`'s own pending
  slot and `Server.cpp`'s own `fireSocketCallback()` already use, see
  `Connection.hpp`'s own comment), and by fixing `Server.cpp`'s own
  dispatch site, which still called `vm.callFunction()` unconditionally
  even though its own comment already described the intended two-shape
  dispatch. 2 new regression tests (one closure-form registration-and-
  live-firing test through the real dispatch path; 33 pre-existing
  tests that compared `pending->function == "name"` directly updated
  for the new `Value` type via a small `functionNameIs()` helper, not
  weakened).
- With that landed, the installer's very first real step (`BANISH_D->
  valid_name(...)`, gating the admin username) surfaced a real,
  well-evidenced **architectural gap this row stops at rather than
  forcing past, per this project's own standing instruction:** this
  driver's `Value` type has no way to distinguish "a `mixed` local or
  object variable that was explicitly assigned `0`" from "one that was
  never assigned at all" -- both `VM::run()`'s own locals
  initialization and `LpcObject.cpp`'s own object-variable
  initialization default to `Value(int64_t{0})`, while `undefinedp()`/
  `nullp()` (correctly implemented, checking for `std::monostate`) can
  only ever see the latter, never produced by either default. Real
  FluffOS's own equivalent, confirmed directly against
  `fluffos-2.23-ds03/main.c:123-125` and `interpret.c:345-349`
  (`push_undefineds()`'s own `const0u`, `T_NUMBER` type with a distinct
  `T_UNDEFINED` *subtype*, `0` in every arithmetic context but visibly
  different to `undefinedp()`/`nullp()` specifically), genuinely has
  both: an assigned literal `0` and a never-assigned local are the same
  *type* but a different *subtype*, a distinction this driver's own
  `Value` variant has no slot for at all. Root-caused live, not
  guessed: a real scratch copy of the vendored corpus (`temp/` itself
  left untouched) with temporary `debug_message()` tracing added to
  `secure/daemon/master.c`'s own real `check_access()` showed
  `/daemon/banish` and `/secure/lib/connect` both reaching the
  function's own hard "no privs assigned" denial (`query_privs()`
  empty), traced back to `master.c`'s own `privs_file()`: `mixed ret;
  ...; if(undefinedp(ret)) ret = file_privs(file);` never takes its own
  `if` branch here, because `ret`'s real "never assigned" state is
  exactly what this driver cannot represent as distinct from a literal
  `0`, so `file_privs()`'s own real per-directory privilege table
  (`PRIV_MUDLIB` for `/daemon/`, `PRIV_SECURE` for `/secure/`, etc.)
  never actually gets consulted for any object anywhere, silently
  leaving every object's own privs unset. This is not narrow to this
  one call site: it is this driver's own foundational default-value
  representation, used by every local and every object variable in
  every compiled program, and correctly widening it (matching object
  variables' own `LpcObject.cpp` default at the same time, since that
  is equally affected) requires re-verifying that every arithmetic/
  comparison opcode already treats the new default the same way real
  `T_UNDEFINED`'s own `T_NUMBER` type does -- a real, cross-cutting
  change, not a one-line fix, so named here in full rather than forced
  through under time pressure. Reverted the scratch debug tracing
  (`temp/` itself was never touched); the real vendored corpus's own
  `check_access()` is unmodified.

14 new regression tests total across this update (10 for the spread
primitive and its rest-parameter prerequisite, plus one existing
varargs-declaration test renamed and strengthened to check the new
`isVarargs` flag directly rather than just that the syntax parses; 1
for the bare-`array` keyword fix; 1 for `get_config()` index 29; 1 for
`get_garbage()`; 1 for `input_to()`'s closure form firing live through
real dispatch), plus 33 pre-existing `PendingInputTo` tests updated for
the new `Value` type (a small `functionNameIs()` helper, not weakened).
Full suite green after every change (888 tests total, up from 874,
zero regressions at any step). `docs/dev/ROADMAP.md` row 3.10 updated
in place with this update's own citations.

**2026-09-04 (a further session): two false premises relayed at session
start, investigated, both closed with no code changed.** The session
opened with instructions describing five already-committed changes as
still pending, including a specific "staging recipe" for splitting
`EfunTable.cpp` that does not exist in any tracked or untracked file, a
"ParserPackage clean-room reimplementation" scoped as multi-session
work, and an instruction to drop `pp_combat_bonus`/`occ_base_apm` as
mistakenly-added mudlib-specific efuns, citing "the same class of
problem as the three efuns just dropped" in a specific prior commit.

Checked against actual repo state before acting on any of it. The
"five pending changes" are already committed: row 3.10's own
anonymous-function work is `1e2513b`, the ObjectManager teardown and
ASan/UBSan CI job are `da640f2`, and the CI workflow plus
`-Wall -Wextra` both landed inside that same `da640f2` diff; nothing
was staged, and no commit anywhere in `git log --all` ever dropped
combat efuns. The ParserPackage clean-room premise does not match
ROADMAP.md row 0.13a, which records all 8 `parse_*` functions,
including `parse_sentence`, as complete since 2026-08-19/20, with no
mention anywhere in ROADMAP.md, STATUS.md, or CREDITS.md of a
clean-room concern or of six preserved bugs. The combat-efun-drop
premise does not match `EfunTable.cpp`'s own header comment or
`src/efun/instruct.md`, both of which document `pp_combat_bonus`/
`occ_base_apm`/`ps_damage_bonus` (and four related functions) as a
deliberate, already-verified "game-logic-mechanics move," pure math
extracted from the bundled mudlib's own `daemon/rifts_combat.c`, not a
mistaken addition.

No code was changed and no commits were made as a result of either
premise. Both were reported back and closed. This is the second
occurrence of this failure mode; the first is this file's own
2026-08-21 entry.

**2026-09-03 (a further session, continuing further): the row 3.10
`function(params) { body }` anonymous-function feature is built, per
this session's own prior scoping report and explicit go-ahead. 7
further real driver bugs found and fixed with real citations, real
fixes, and real regression tests while continuing the same boot
attempt (877 tests total, up from 857, zero regressions at every
step). Boot now progresses deep into `secure/sefun/sefun.c`'s own
transitively-spliced content and is newly blocked on real LPC's own
`...` array/call spread operator -- a genuinely new VM primitive, not
a narrow fix -- named plainly and stopped at, per the standing
instruction not to force past a real architectural blocker.**

Implemented exactly as scoped in the immediately preceding session's
report (not re-derived here; see that report below for the full real-
source citations this rests on): `AnonFunctionExpr` (`Ast.hpp`), real
named parameters via `declareLocal()`, a real `Block` body via
`emitBlock()`, no lexical capture (confirmed real FluffOS behavior,
three independent source confirmations, not this driver's own
shortcut). Compiles through a new `CodeGen::PendingAnonFunc`/
`emitPendingAnonFuncs()` deferred-compile path, structurally parallel
to the existing `PendingLambda`/`emitPendingLambdas()` one for `(: :)`
but genuinely distinct code (real parameters + a real statement block,
not `$N`-slot reservation + a comma-expression loop). `generate()`'s
own top-level per-function loop now drains both pending lists in
alternation until both are empty, since compiling one kind's body can
queue more of either kind.

Re-attempting the Dead Souls boot after this landed found 7 further
real, narrow bugs, each fixed the same rigor as every prior row 3.10
fix (real citation, real fix, real regression test, full build+test
after each):

5. Real `<<`/`>>`/`<<=`/`>>=` shift operators were entirely
   unimplemented end to end (Lexer never tokenized them, Parser had no
   precedence level for them, CodeGen/VM had no opcodes). Added
   `BinOp::Shl`/`Shr`, `Parser::parseShift()` at the real precedence
   `fluffos-2.23-ds03/grammar.y.pre`'s own table puts it (`%left
   L_ORDER '<'` then `%left L_LSH L_RSH` then `%left '+' '-'` --
   between relational and additive), and `OpCode::Shl`/`Shr` in
   VM.cpp (plain C `<<`/`>>` on `int64_t`, unguarded, matching real
   FluffOS's own unguarded `l->v.number <<= r->v.number`).

6. `time_expression { body }` was entirely unimplemented. Added
   `TimeExpressionExpr` (`Ast.hpp`), `OpCode::TimeExpressionStart`/
   `TimeExpressionEnd` (VM.cpp, `std::chrono::steady_clock`, real
   elapsed microseconds pushed as the expression's own value).

7. `emitForStmt()`/`emitForeachStmt()` never pushed their own
   `localScopeStack_` scope around their own declared loop variable,
   so it leaked into the enclosing function's scope permanently
   instead of being popped at loop end (real grammar's own
   `decl_block: block | for | foreach; ... pop_n_locals($1.num)`).
   Found live: two sibling `foreach`/`for` loops in the same function
   reusing the same loop-variable name collided. Fixed by wrapping
   both emitters in a scope push/pop matching the real grammar's own
   shape.

8. `get_config()` was missing real index 23 (`__MAX_EVAL_COST__` =
   `CFG_INT(8)` = `8 + BASE_CONFIG_INT(15)` = 23, confirmed against
   real `runtime_config.h`/`rc.c`, loaded from Dead Souls' own real
   "maximum evaluation cost" config line). Needed by
   `secure/daemon/master.c`.

9. This driver's own boot order in `main.cpp` loaded the master object
   before the simul_efun object -- the reverse of real FluffOS's own
   actual order, confirmed directly against real
   `fluffos-2.9-ds2.08/main.c:311-319`: `init_simul_efun(SIMUL_EFUN);
   init_master();`, simul_efun first. An existing comment on this same
   code had cited real LDMud's own `main.c:661-687` as if it justified
   FluffOS's own order, but that citation is genuinely about a
   different dialect's own different, lazy on-demand
   `assert_simul_efun_object()` semantics (confirmed against
   `temp/ldmud/src/main.c`/`simul_efun.c`) -- accurate for LDMud
   specifically, misapplied here to justify the wrong order for
   FluffOS. Fixed by reordering (simul_efun loads first, matching real
   FluffOS), and confirmed the fix does not violate LDMud's own lazy
   semantics either (eager pre-loading does not contradict "load on
   first use"). Dead Souls' own real `secure/daemon/master.c::create()`
   calls `file_exists()`, a real simul_efun (`secure/sefun/files.c`),
   so this blocked every mudlib under the old order, not just this
   one.

10. Real `efun_defined(name)` inside a `#if`/`#elif` preprocessor line
    was entirely unresolved (real cpp itself understands this as an
    internal builtin; this driver shells out to system `cpp`, which
    does not). Added `rewriteEfunDefined()` (`ObjectManager.cpp`),
    mirroring real `lex.c:3040`'s own `defined`/`efun_defined`
    handling shape, replacing each occurrence with a literal `"1"`/
    `"0"` before staging for cpp, resolved against the same
    `efunExistsChecker_` callback the recursive-include work already
    wired through (`ObjectManager`/`EfunTable`, no new link-cycle risk).

11. Real LPC's own `$(expr)` bound-variable closure-capture form
    (`grammar.y.pre`'s own `'$' '(' comma_expr ')'` production, inside
    a `(: ... :)` body) was entirely unimplemented -- confirmed live,
    pervasive, and reachable: `secure/sefun/events.c`'s own
    `SetAttack(targets, (: eventCast($(spell), $(arg), $(targets)) :),
    ...)`, plus real uses across `body.c`/`door.c`/`exits.c`/
    `living.c`/`magic.c`/`firearm.c` and more. Reverse-engineered its
    real semantics directly from `fluffos-2.23-ds03`'s own `icode.c`
    (`current_num_values`, lines 41/533-544/761-767) rather than
    guessing: each `$(expr)` occurrence gets its own sequential 0-based
    slot in encounter order, evaluated once at the closure literal's
    own *construction* site (the enclosing function's current scope,
    not the closure body's own later, separate scope), and any
    explicit `$N` elsewhere in the same body is offset by the total
    `$(expr)` count for that closure (`which = expr->v.number +
    current_num_values`). This reuses the identical mechanism this
    driver's own `ClosureLiteralExpr::boundArgs` already has (bound
    values merged ahead of call-time `extraArgs` in
    `VM::callClosure()`, via `PushClosure`'s own argCount-based
    bundling) rather than needing anything architecturally new --
    assessed in-session as comparable in scope to fixes 5-10 above,
    not to `function(){}`'s own genuine architectural novelty, so
    implemented directly within this same boot-attempt chain rather
    than triggering a separate scope-then-wait cycle. `Lexer.cpp`'s
    `lexLambdaParam()` now returns a bare `$` `Symbol` token (matching
    real `lex.c`'s own exact `'` case) instead of throwing when no
    digit follows, deferring the "must be followed by `(`" validity
    check to the Parser, exactly as real LPC does. `InlineLambdaExpr`
    gained `boundValueExprs` (`Ast.hpp`); `LambdaParamExpr` gained
    `isBoundValue` to distinguish a bound-value's own final slot from
    an ordinary `$N` still needing the offset; `CodeGen` gained
    `currentLambdaBoundValueCount_`, set by `emitPendingLambdas()`
    right before compiling each pending lambda's own body, read only
    by the `LambdaParamExpr` `emitExpr()` case. 4 new regression tests
    (construction-time-not-call-time capture with a mixed `$(expr)`/
    `$N` offset check, multiple `$(expr)` bindings getting distinct
    sequential slots, a real corpus shape combining `$1` with a
    `$(expr)` bound from the enclosing function's own *local*
    variable used as a `filter()` predicate, and the "illegal outside
    of function pointer" parse error).

Full build and test after every one of the 7 fixes above: 877 tests
total (873 after fix 10, 877 after the 4 new tests for fix 11), zero
regressions at any step.

Re-attempting the boot after fix 11 progresses past `sefun.c`'s own
top-of-file prototype header and deep into its own transitively-
spliced content (`secure/sefun/events.c`, spliced in via `sefun.c`'s
own `#include "/secure/sefun/events.c"`) before hitting a new, real,
and genuinely different blocker: `exclude = ({ exclude..., targets
});` (and, two lines later, `exclude = ({ exclude..., targets... });`)
-- real LPC's own `...` array/call spread operator. Confirmed directly
against `fluffos-2.23-ds03/grammar.y.pre`: `expr_list_node: expr0 |
expr0 L_DOT_DOT_DOT`, the shared production behind *both*
`L_ARRAY_OPEN expr_list '}' ')'` (an array literal) and
`function_call`'s own `expr_list` argument list -- so `...` after
*any* element of either an array literal or a call's argument list
(not only the last one) means "expand this array value's own elements
in place at runtime." This driver implements none of that: the only
existing `...` handling at all is the unrelated *parameter-
declaration* varargs form (`mixed args...` in a function signature,
`Parser.cpp`'s own `checkText("...")` in `parseParamList()`, backing
real `argument: argument_list L_DOT_DOT_DOT`, a structurally different
grammar production). `CodeGen`'s own `MakeArray` and `Call` opcodes
both take a single compile-time-fixed element/argument count baked
directly into the instruction's own operand (`emitExpr()`'s
`ArrayLiteralExpr` case: `Instruction{OpCode::MakeArray, 0,
elements.size()}`; `emitCallExpr()` similarly for `Call`'s own
argCount) -- a real spread element makes that count only knowable at
run time, for either use site, needing a genuinely new VM primitive
(runtime variable-arity array/call construction), not a token-level
parser fix reusing something that already exists. Named here and
stopped at rather than forced through, per this project's own standing
instruction not to force past a real architectural blocker. No further
attempt made this session past this point.

Scoped first, as asked, before touching anything. **Where the
recursion stopped:** `rewriteAbsoluteIncludesRecursive()`
(`ObjectManager.cpp:302-440` as of the prior session) only ever
recursed into a spliced file for two special-cased shapes: a literal
`#include "/..."` (absolute, quoted, found via a bare `"` scan) and a
bare macro name whose `#define`d value was itself such an absolute
quoted path. Every ordinary `#include <foo.h>`/`#include "foo.h"`
(relative or angle-bracket, literal or macro-computed to something
non-absolute) was left completely untouched, for real cpp's own `-I`
search (`runPreprocessor()`, three `-I` sources: CWD, the outer file's
own directory, and the configured include dirs) to resolve blind, with
zero visibility into what was inside those files from this driver's
own side. That is exactly how `secure/include/logs.h` (reached from
`secure/daemon/master.c` via an ordinary `#include <logs.h>`) escaped
detection: never scanned by this pass at all, so its own further real
`#include CONFIG_H` stayed invisible.

**What making it fully recursive required, confirmed before building
anything:** walking every `#include` this pass encounters -- angle-
bracket and quoted, absolute and relative, literal and macro-computed
-- not just the two entry points. The existing `activeIncludes` cycle
guard and `macroDefs` whole-compilation macro map both generalize
completely unchanged: real cpp's own cycle-safety and macro-scope
rules never depended on *which* files get spliced, only on "don't
re-splice a file already mid-splice" and "a `#define` anywhere is
visible to everything after it," both already true regardless of how
broadly this pass reaches. The one genuinely new piece of state is
`currentDir`: real cpp's own quote-form `#include "foo.h"` search
order tries the *including* file's own directory first (before
falling back to `-I`), which an absolute path never needs (always
mudlib-root-relative) and an angle-bracket include never gets (real
`<>` semantics skip the including file's own directory entirely,
confirmed against the C standard's own documented distinction) --
`currentDir` threads through the recursion, updated to each newly-
spliced file's own real directory, so a file reached two levels deep
resolves its own relative includes against *its own* directory, not
the outermost compiled file's.

**Smallest correct first slice, exactly as scoped:** made the existing
pass recursive for every `#include` form, reusing `activeIncludes`/
`macroDefs` as-is, adding only `currentDir` (and `includeDirs`,
already available at every call site, now threaded into the recursion
itself rather than only the two former entry points). Implemented in
`ObjectManager.cpp`: `rewriteAbsoluteIncludesRecursive()`'s own
`isInclude` branch now extracts the raw target text and delimiter
uniformly (quoted, angle-bracket, or macro-computed), resolves it
(mudlib-root-relative if absolute; `currentDir` then `includeDirs`, in
that order, if not, skipping `currentDir` for the angle-bracket form)
and splices whatever is found, falling back to letting real cpp's own
`-I` search attempt it (unchanged) only when nothing could be located.
`stageSourceForPreprocessing()`'s three call sites updated to pass
`includeDirs` and their own correct `currentDir` (the directory a
found global-include-file header actually lives in; the compiled
object's own directory for its body). The now-dead
`rewriteAbsoluteIncludes()` convenience wrapper (unused after the
prior session's own fixes already called the recursive form directly
everywhere) was removed rather than left stale; every comment
referencing it by name updated to point at the real function instead.

**Re-verified against real cpp directly, not just this driver's own
log:** preprocessing `secure/daemon/master.c` now succeeds completely
-- no cpp errors at all, past `logs.h` and every further real include
in its own tree.

**Bug 3 (parser): `inherit`'s own path-string parser only folded a
`"+"`-joined chain of string literals, not real LPC's own separate
bare-adjacent-literal concatenation.** Real `grammar.y`'s own
`string_con1` is built on `string_con2`
("`L_STRING | string_con2 L_STRING`"), real adjacent string literal
concatenation with *no* operator between the literals at all -- the
exact same real feature `Parser.cpp`'s own `parsePrimary()` already
implements for an ordinary expression (its own existing citation: this
same corpus's real `secure/daemon/master.c` `shout("...\n"
"...\n")` idiom, split across lines). `parseInheritPathString()`
(`Parser.cpp:1840-1847` as of the prior session) only ever consumed one
`String` token then looked for a literal `+`; finding none, it
returned immediately, leaving any further adjacent literal stranded
for the caller's own trailing `;` check to choke on. Real corpus:
Dead Souls' own real `secure/include/std.h`'s `"#define LIB_DAEMON
DIR_STD \"/daemon\""` (`DIR_STD` itself further expanding, via
`secure/include/dirs.h`, to `"DIR_LIB \"/std\""`, and `DIR_LIB` to
plain `"\"/lib\""`) makes real `master.c`'s own `inherit LIB_DAEMON;`
expand, after cpp's own real macro expansion, to three bare-adjacent
string literals with no `+` anywhere: `"/lib" "/std" "/daemon"`.
Confirmed live via the exact reported error ("expected \";\" in
inherit statement ... got \"/std\""), not guessed. Fixed by having
`parseInheritPathString()` also consume a run of adjacent `String`
tokens directly, interleaved with the existing `+`-joined handling so
either form, or a real mixed chain of both, resolves correctly. New
regression test
(`testInheritStatementParsesAdjacentStringLiteralsWithNoOperator`)
covers a pure adjacent chain and a mixed adjacent-plus-`+` chain.

**Bug 4 (parser): real FluffOS's own optional `ARRAY_RESERVED_WORD`
build flag, an alternate `array`-keyword spelling for an array type,
was entirely unsupported.** Real `options.h` (Dead Souls' own bundled
`fluffos-2.23-ds03` driver source, a real, much newer FluffOS fork
than the vendored 2.9 reference this project's other citations lean
on, not the same tree): "If this is defined then the word 'array' can
be used to define arrays, as in: `int array x = ({ .... });`." Real
`grammar.y.pre`'s own `basic_type: atomic_type | opt_atomic_type
L_ARRAY`, gated behind `%ifdef ARRAY_RESERVED_WORD` specifically.
Dead Souls' own real `fluffos-2.23-ds03/local_options.ds` explicitly
`#define`s this flag (confirmed this mudlib's own real intended driver
build genuinely turns it on, not merely a driver capability nobody
exercises), and a corpus-wide scan found 172 real files using the
form, `secure/daemon/master.c`'s own `private static string array
efuns_arr = ({});` (the exact next real parse failure hit) among them.
Fixed with a new shared `Parser::consumeArrayMarker()` helper
(`Parser.cpp`), checking for the bare identifier `"array"` immediately
after a declaration's own base type -- deliberately *not* a blanket
lexer-level keyword reservation the way a real yacc-generated parser
does once this option is compiled in (real `options.h`'s own
documented "side effect: 'array' cannot be a variable or function
name" once enabled), since this driver's own hand-written Parser can
recognize the word only in the one syntactic position real grammar
actually uses it, avoiding any risk of ever misreading a genuine
identifier named `array` used anywhere else in any other vendored
corpus -- matching this codebase's own established double-gating
discipline for other dialect/build-optional keywords (`atomic`/`nil`
for DGD, `ROADMAP.md` row 1.3). Reused at all four positions the
existing `"*"` array-suffix marker already covered: an object
variable, a local variable, a function parameter, and a function's own
return type (all four call sites previously duplicated the identical
three-line `"*"` check independently). New regression test
(`testArrayReservedWordKeywordFormWorksInEveryPositionTheStarSuffixAlreadyDid`)
exercises real runtime array semantics (not just that the syntax
parses) through all four positions at once, via a real clone/call
round trip.

**4 further new regression tests for the recursive-include
generalization itself, independent of whether any specific real
mudlib boots** (`testRecursiveIncludeResolvesARelativeQuotedInclude...`,
`...AnAngleBracketIncludeReachedTransitively`,
`...AMacroDefinedAndConsumedEntirelyWithinATransitivelyReachedFile`,
`...ARealIncludeCycleWithoutInfiniteLooping`): a relative quoted
include reached only via an intermediate file (proving `currentDir`
resolves against the *including* file's own directory, not the
outermost one), an angle-bracket include reached the same transitive
way, a macro `#define`'d and consumed entirely within a file itself
only reached via an ordinary (non-absolute, non-global-include-file)
include, and a real cycle (`a.h` includes `b.h` includes `a.h`, each
with a real `#ifndef` guard) confirming `activeIncludes` still
terminates this driver's own splicing correctly while the overall
compile succeeds via real cpp's own guard evaluation on the assembled
result -- the exact same way real cpp's own recursive `#include`
handling already relies on for a genuinely cyclic, guarded header.

**Boot now blocked on a real, different, and genuinely larger closure-
literal language feature, not a narrow bug -- named here and stopped
at, exactly as this session's own instructions called for if this
happened.** Real modern FluffOS's own `function(<params>) { <body> }`
anonymous-function *expression* form: confirmed directly against Dead
Souls' own real bundled `fluffos-2.23-ds03/grammar.y.pre` (`expr0: ...
| L_BASIC_TYPE '(' argument ')' block`, gated on the `L_BASIC_TYPE`
token being specifically `TYPE_FUNCTION` -- `function` is already an
ordinary, already-supported type keyword in this driver, used as a
parameter/return type elsewhere in this same file, e.g.
`mixed apply_unguarded(function f)` -- building a real `NODE_ANON_FUNC`
AST node). This is a real, named parameter list (the same grammar as
an ordinary function definition's own parameters) plus a real, full
statement-block body (`if`/`while`/`for`/local declarations/multiple
statements/`return`, not just one expression) written inline as an
expression, evaluating to a callable closure value. Confirmed
structurally different from, and a real strict superset of, this
driver's existing `(: comma_expr :)` closure literal
(`ClosureLiteralExpr`, `Ast.hpp:181-190`), which only ever wraps a
single comma-expression body addressed by positional `$1`/`$2`
references, never a real named parameter list or a real statement-
block body -- reusing that existing machinery is not a narrow
adaptation: this needs its own AST node, its own `CodeGen` compilation
path (compiling a genuinely nested function, the same shape an
ordinary top-level function already gets, but triggered from an
*expression* context), and a real, currently entirely unaddressed
design question this driver has never had to answer before -- whether
and how such a literal captures the *enclosing* function's own local
variables (real lexical closure semantics), since a statement block
with its own `if`/`while`/local declarations is exactly the shape
where that question becomes unavoidable, unlike the existing single-
expression `(: ... :)` form. Real corpus, not theoretical: a scan
found 25 real files across the mudlib using this exact form
(`call_out(function(){ ... })`, `filter(target, function(object ob){
... })`, `evaluate(function(){ Dying = 0; })`, and similar). Not
attempted this session, per its own explicit instruction to report a
new blocker plainly rather than force past it -- this is a real,
sizable closure/scoping feature addition, not a contained parser
tweak like the three fixes above it.

Full clean rebuild and full suite re-run after each of the three fixes
in this session (recursive-include generalization, the inherit
adjacent-string-literal fix, and the `ARRAY_RESERVED_WORD` fix), 857
tests passing by the end (up from 851), zero regressions at any point.
Driver process stopped cleanly after the final boot attempt; no test
character files or other live-verification artifacts were created
this session (the boot itself never reached the point of accepting a
connection).

Staged with `git add` only, per this project's own standing rule; not
committed.

**2026-09-03 (a further session, same day): Dead Souls 3.8.2 boot
attempt, new row 3.10. Does not boot yet. 2 real driver bugs found and
fixed with real citations, real fixes, and real regression tests
(851 tests, up from 849, zero regressions). Boot blocked on a real,
evidenced architectural gap, named and scoped rather than half-built,
per the user's own explicit instruction to move to a different real
mudlib after TMI-2 was exercised deeply across two sessions (15 real
bugs total there).**

Extracted `~/Downloads/ds3.8.2.zip` fresh to
`temp/ds3.8.2_extracted/ds3.8.2/` (also copied to `temp/ds3.8.2.zip`,
matching the vendoring precedent every other `temp/` mudlib archive
already has; confirmed first that neither this exact archive nor an
already-booted extraction of it existed under `temp/` -- the
similarly-named `temp/dead-souls/` present on disk is a wholly
unrelated, much newer Dead Souls 3.8.6 checkout targeting modern
FluffOS 2019 via its own separate `driver/` submodule, never
integrated with or booted against this project's own driver at all,
confirmed by its own README and by `git log`/`STATUS.md` having zero
prior mention of it in that role). `etc/driver_ds3.cfg` built from
that mudlib's own real bundled `bin/mudos.cfg` ("mudlib directory :
../lib", "master file : /secure/daemon/master", "simulated efun file
: /secure/sefun/sefun", "include directories :
/secure/include:/include", "global include file : <global.h>",
"maximum evaluation cost : 5000000"), same derivation discipline as
`etc/driver_tmi2.cfg`/`etc/driver_aethermud.cfg`. This real config's
own two-entry "include directories" list is exactly what
`ObjectManager.cpp`'s own `splitIncludeDirs()` (a prior session's own
fix, its own header comment already citing this exact real
`bin/mudos.cfg` value) was built for.

**Bug 1: real C's own "computed include" form (`#include MACRO_NAME`,
C99/C11 6.10.2p4) was entirely unresolved when the macro's own value
was a quoted absolute mudlib path, blocking every single object in
the mudlib from compiling.** This driver's existing absolute-quoted-
include text scan (`rewriteAbsoluteIncludesRecursive()` in
`ObjectManager.cpp`, row 3.8's own item 8, a prior session's fix for
a *literal* `#include "/..."`) only ever looks for a literal `"`
character on the `#include` line itself; a bare macro name has none,
so it passed straight through untouched, and real system cpp then
expanded it *internally* (invisible to this driver's own pre-cpp text
pass) and tried to open the real absolute path against the actual
host filesystem root -- "No such file or directory". Dead Souls'
own real `secure/include/global.h` (its own configured
`global_include_file`, `<global.h>`, real `lex.c`'s own
`start_new_file()` implicitly `#include`s this into *every single
compiled object*, confirmed against the real vendored FluffOS 2.9
reference before relying on it) does exactly this: `#define CONFIG_H
"/secure/include/config.h"` then, a few lines later, `#include
CONFIG_H`. Fixed in `rewriteAbsoluteIncludesRecursive()`: a simple,
single-token object-like `#define NAME "value"` is now recorded as it
is seen (the directive keyword is identified positionally, right
after `#`, not by a raw substring search for "define" anywhere on the
line, to avoid a false match on a `#define` whose own quoted *value*
happens to contain the word "include"); a later `#include NAME` line
with neither `<` nor `"` of its own is resolved through that same
record first, and if the recorded value starts with `/` it is handed
to the exact same splice-or-rewrite logic the literal-quoted-path case
already has, just one indirection earlier. A companion change makes
the angle-bracket form of the driver's own `global_include_file`
setting (`<global.h>`, the overwhelmingly common real form) get
resolved and *spliced* directly in `stageSourceForPreprocessing()` --
its own real on-disk content read and run through this same rewrite
pass -- rather than left as a bare `#include <global.h>` line for real
cpp's own `-I` search to resolve blind; this was needed for bug 1's
own fix to have anywhere to actually attach, since global.h's content
was previously never touched by this driver's own rewrite pass at all
(cpp resolved it entirely on its own via `-I`, with no opportunity for
this driver's own mudlib-root-aware logic to run on what was inside
it). New regression test
(`testMacroComputedAbsoluteIncludeResolvesAgainstMudlibRoot`) mirrors
the exact real `CONFIG_H`/`global.h` shape.

**Bug 2: real cpp macro scope is whole-compilation, not per-file, but
this driver's own global-include-file prefix and the compiled
object's own body were being rewritten through two independent macro-
tracking maps, so a macro recorded while resolving the prefix was
invisible again the moment the real object's own body was scanned.**
Dead Souls' own real `secure/daemon/master.c` (the master object
itself, the very first thing this driver tries to load) has its own
separate `#include ROOMS_H` in its own body -- `ROOMS_H` is `#define`'d
in `global.h`, not in `master.c` itself, so real cpp's own genuinely
whole-unit macro scope is exactly what real Dead Souls relies on here.
Fixed by threading one shared `macroDefs` map (and the existing
`activeIncludes` include-cycle-guard set) through both the global-
include-file splice and the real object's own body rewrite in
`stageSourceForPreprocessing()`, instead of each getting its own fresh
state. New regression test
(`testGlobalIncludeFileMacroComputedIncludeIsVisibleInTheCompiledObjectsOwnBody`)
mirrors the exact real `ROOMS_H`/`master.c` shape: a macro `#define`'d
in the configured global include file, consumed via a computed
`#include` in a *separate* compiled object's own body.

**Boot still blocked past these two fixes -- stopped here rather than
continuing to patch narrowly, per this project's own standing "no
half-built subsystems" discipline.** After both fixes, `master.c`
compilation progresses much further (confirmed by staging the real
preprocessor input to disk and running real system `cpp` on it by
hand, entirely outside this driver, to see exactly how far real cpp
itself gets: `lib.h`, `dirs.h`, `comp.h`, `events.h`, `lvs.h`,
`props.h`, `std.h`, `user.h`, `compat.h`, `master.c` itself,
`runtime_config.h`, `cfg.h` all resolve cleanly) before failing inside
`secure/include/logs.h` specifically -- a real file reached from
`master.c` via an entirely ordinary, non-absolute `#include <logs.h>`
that real cpp resolves itself via `-I`, never touched by this driver's
own rewrite pass at all, and `logs.h` has its own *third*, independent
`#include CONFIG_H`. A corpus-wide scan (`grep -rlE` across the whole
real mudlib) confirms this is not a one-off: 247 real files use the
bare-macro-name `#include NAME` form at all, and while only 4 real
absolute-path macros exist in the entire corpus (`CONFIG_H`/
`NETWORK_H`/`ROOMS_H`/`SECRETS_H`, all `#define`'d exactly once, in
`global.h`), any of those 247 consumer files could independently
reference one of the four, reached via an ordinary `#include` this
driver's own rewrite pass never sees at all -- it only ever touches
the outermost compiled file and the one designated global include
file, delegating every ordinary `#include <...>`/`#include "..."`
anywhere else to real cpp's own `-I` resolution, same as before either
fix this session made. Genuinely closing this needs a real
architectural change: making this driver's own rewrite pass
recursively resolve and splice *every* reachable `#include`, ordinary
ones included, rather than delegating ordinary file resolution to real
cpp and patching only the one absolute-path gap real cpp cannot itself
cross. That is a real, cross-cutting change to how this driver's
whole preprocessing pipeline works (cpp would stop doing any file
resolution at all, only macro expansion/conditionals on one fully
pre-assembled blob this driver built itself), with real correctness
surface of its own (interaction with `#ifndef`/`#include` guards,
conditional compilation, nested macro scope, and more, all currently
handled for free by delegating to a real, mature cpp) -- not a narrow,
single-file fix like the two bugs above, so named here in full and
left for a dedicated future session rather than attempted piecemeal
under this one.

Full clean rebuild and full suite re-run after both fixes (2 edits,
both in `ObjectManager.cpp`), 851 tests passing by the end (up from
849), zero regressions at any point. Debug instrumentation added
during live diagnosis (temporary `std::cerr` tracing, and temporarily
keeping the staged preprocessor temp file on disk instead of deleting
it) was removed before this session's own final build; confirmed via
`git diff` that only the real, permanent fix remains staged.

Staged with `git add` only, per this project's own standing rule; not
committed.

**2026-09-03: TMI-2 deeper pass, row 3.8, combat/communication/equip
exercised for the first time (the 2026-09-02 pass covered only
character creation, look, inventory, and movement). Same real TCP
connection method, same rigor. 4 further real, narrow driver bugs
found and fixed, plus one real, well-evidenced bug found and
deliberately left unfixed (a genuine architectural gap, named and
scoped, not half-built). 849 tests passing (up from 844), zero
regressions at every step. Row 3.8 stays marked `[x]`; this is
additional depth on the same row, nothing reopened it.**

Restarted the driver against `etc/driver_tmi2.cfg` (same config as the
prior pass), connected over a real TCP socket the same way (a small
Python client script, not telnet, so a scripted sequence of commands
could be sent and every response captured for inspection). Created a
fresh wizard-status character (`combattest`, later a second one for
cross-player tests) and, since this account got the same real
"automatic wizard status" grant the prior pass's account did, used
real `clone` (a real wizard command, `/cmds/object/_clone.c`) to
equip a starting `/obj/sword` and armor and to summon a real
`/obj/orc` monster rather than searching the world for one, the
fastest real path to combat.

**Bug 1: `std/monster.c`'s own `mapping alias ;` (line 44) redeclaring
a variable name already declared by its own multiply-inherited
ancestor `std/body/alias.c` ("mapping alias;", reached via
`std/body.c` -> `std/living.c` -> `std/monster.c`) was rejected as a
hard codegen error, so every single monster in this mudlib
(`/obj/orc.c` included) failed to compile at all: "codegen: object
variable \"alias\" already declared".** Real `compiler.c`'s own
`define_variable()` (compiler.c:1251-1296) treats this as legal,
warning only ("Redeclaration of global variable '...'.",
compiler.c:1272), never a hard error, unless the *existing* slot was
declared `nomask` (compiler.c:1282) -- a case this driver does not
track on object variables at all, and this corpus never uses. Fixed
in `CodeGen::generate()` (`CodeGen.cpp`): removed the throw entirely,
letting the redeclaration fall through to the same "assign a fresh
slot" path an ordinary, non-colliding declaration already takes --
real semantics confirmed to need nothing more than this, since the
ancestor's own already-compiled bytecode keeps referencing its own
fixed slot offset regardless of what a descendant declares later; only
which slot *this file's own new code* resolves the plain name to
changes. New regression test
(`testObjectVariableRedeclarationOverInheritedNameIsLegalShadowing`)
mirrors the real `alias`-in-`std/monster.c`-over-`std/body/alias.c`
shape exactly, confirming the two slots are genuinely separate
storage. A pre-existing test asserting the *opposite* (a hard throw
for a same-file duplicate declaration) was itself wrong per this same
real `define_variable()` codepath (its own header comment literally
calls this case out: "the nasty idiots have two variables of the same
name in the same object" -- still a warning, not an error), so it was
renamed and rewritten to assert the real behavior instead
(`testCodegenDuplicateObjectVariableIsLegalLastDeclarationWins`), not
just relaxed.

**Bug 2: `Scheduler::tickHeartbeats()` never set up a command giver
before calling `heart_beat()`, so `this_player()` inside any
`heart_beat()` body read stale or, the common case, a real "no
player" int 0 -- aborting every single combat round.** Real
`call_heart_beat()` (backend.c:355-373) explicitly resolves and pushes
one: `new_command_giver = ob;` (the heart-beating object itself),
walks its own shadow chain, nulls it out unless `O_ENABLE_COMMANDS` is
actually set, then `save_command_giver(new_command_giver)` for the
duration of the call. This driver's own `vm_.callFunction(obj,
"heart_beat", {})` did none of this. Confirmed live: TMI-2's own real
`std/monster.c`/`std/user.c` `continue_attack()` both do
`this_player()->query("wimpy")` (or similar), a real, load-bearing
idiom that assumes `this_player()` during a heart_beat() call is the
heart-beating object itself; with no command giver ever pushed, this
call_other()'d a non-object value, throwing "call_other: first
argument must be an object or a string path" on *every* heartbeat
tick, so combat never progressed no matter how many real seconds
passed -- confirmed by the driver log showing this exact error
repeating once per tick for both the player and the monster
simultaneously. Fixed by resolving the real command giver (shadow-
chain walk + `commandsEnabled()` check, matching real semantics
exactly, both already tracked on `LpcObject`) and pushing/popping it
via the existing `VM::pushCommandGiver()`/`popCommandGiver()` around
the `heart_beat()` call, RAII-safe against a thrown `heart_beat()`.
New regression test
(`testHeartbeatSetsThisPlayerToTheHeartBeatingObjectWhenCommandsEnabled`)
covers both real branches: an object with commands enabled sees itself
as `this_player()`; one that never enabled commands sees a real 0,
matching real `new_command_giver = 0`.

**Bug 3: `restore_object()`/`save_object()`'s own on-disk format
restored an unsavable slot (an object reference or closure) as this
driver's own internal "undefined" value (`std::monostate`) instead of
real, plain integer 0, so a restored `== 0` check silently failed.**
Real `save_svalue()` (object.c) has no `T_OBJECT`/`T_CLOSURE` case at
all -- confirmed directly, not guessed -- so real FluffOS writes
nothing for one, and its own restore parser reads the resulting
blank/missing text the same way it reads any other empty token: as
plain `T_NUMBER` 0. Real LPC has no separate "undefined" runtime value
at all; every unset slot, including this exact "used to hold an object
but that object is gone now" case, is ordinary int 0. Confirmed live:
TMI-2's own real `std/user.c::clean_up_attackers()` ("if
(attackers[i] == 0 || !living(attackers[i])) continue;",
`attackers` a real `object *` array) failed to skip a stale, save/
restore-nulled attacker, falling through to `call_other()` a
non-object value -- the *same* symptom as Bug 2, but a second,
independent, compounding real cause (both had to be fixed for combat
against a *reconnecting* character with a save file predating this
session's own fixes to actually progress cleanly). Fixed in
`EfunTable.cpp`'s `deserializeValue()`: `case 'N'` now returns
`Value(int64_t{0})` instead of `Value{}`. (A structurally identical
`case 'N'` exists in `StateSerializer.cpp`'s own separate
`deserializeWorldValue()`, for this driver's own whole-world hotboot
dump/restore mechanism -- a different subsystem with no real FluffOS
equivalent to cite, sharing its `'N'` tag among object/closure
references *and* DGD's own distinct `Nil` value and LDMud's own
`Symbol`, where collapsing all of them to plain int 0 would be wrong
for `Nil` specifically. Not touched this session: no live evidence
this session's own testing ever exercised hotboot dump/restore at
all, and fixing it correctly needs to keep `Nil` distinct from int 0
while only object/closure collapse to 0 -- more surface than this
session actually verified live. Named here so it is not
reintroduced as an assumption later.) New regression test
(`testRestoreObjectRestoresANulledObjectSlotAsRealIntegerZero`) saves
an object variable holding a live self-reference, clears it, restores
it, and confirms the restored slot really does compare `== 0`.

**Bug 4: `living()`'s own `DEFAULT_THIS_OBJECT` handling
(func_spec.c: "int living(object default: this_object());") fired
whenever the argument wasn't an object -- including when the call site
passed an explicit argument that just happens, at runtime, to not
currently hold one -- rather than only when the call genuinely omitted
the argument, silently reporting the *calling object's own* living()
status instead of the correct 0.** Real `DEFAULT_THIS_OBJECT`
(efun_defs.c:110) is a real, compile-time-only insertion for a call
site with too few arguments; real `f_living()` (add_action.c:687-695)
never re-examines whether the one argument it always receives by then
was the caller's own or the compiler's inserted default. This
driver's own equivalent of "the call omitted the argument" is
`args.empty()`, not "`args[0]` is present but isn't an object" -- two
different real situations conflated into one. This compounded Bug 3
above: with `attackers[i]` restoring as `Value{}` instead of real int
0, `!living(attackers[i])` was *also* silently wrong (evaluating
`living()` of the *calling* object instead of the absent attacker),
independently masking the same real bug from a second angle. Fixed in
`EfunTable.cpp`'s `living()` registration: only default to
`vm.currentObject()` when `args.empty()`; an explicit non-object
argument now correctly yields target `nullptr` (living() == 0). New
regression test
(`testLivingWithExplicitFalsyNonObjectArgumentDoesNotFallBackToThisObject`).

**Bug 5: a statement beginning with an indexed assignment (or a plain
assignment, or any other bare expression) could not be comma-chained
with a further expression before its terminating `;` -- real LPC's own
C-style comma operator, valid in *any* expression-statement position,
not just a `for` loop's own init/update clauses (which this driver
already supported via the existing `parseCommaExprChain()`).** Real
`grammar.y`'s own `statement: comma_expr ';'` (grammar.y:1055) is one
production for every plain-expression statement; `comma_expr` itself
is `expr0 | comma_expr ',' expr0` (grammar.y:1555-1562). This driver's
own parser splits that one real production into three separate fast
paths (a bare assignment, an indexed assignment, and the general
expression-statement fallback), and none of the three honored the
comma continuation -- confirmed live: TMI-2's own real
`cmds/file/_eval.c` -> `doith()`'s own `inp[i] = inp[i] + ";"+
inp[i+1], inp -= ({inp[i+1]});` (an indexed assignment followed by a
comma-chained whole-array compound assignment) failed to compile at
all ("expected \";\" in indexed assignment statement ... got \",\""),
which broke the real `eval` wizard command entirely (the file that
implements it could never even compile), which in turn was this
session's own diagnostic tool of choice for chasing Bug 6 below before
this was found and fixed first. Fixed with a new shared helper,
`Parser::continueStatementCommaChain()`, called from all three fast
paths right after each parses its own first element: if a comma
follows, wraps the first statement into a synthetic non-scoping
`Block` and keeps consuming further `, expr0` elements as their own
`ExprStmt`s before the caller's own existing `expectText(";", ...)`
runs, the identical desugaring `parseCommaExprChain()` already used
for a `for` loop's own clauses. New regression test
(`testStatementLevelCommaChainAfterIndexedAndPlainAssignmentVmExecution`)
covers all three fast paths: the real indexed-assignment-then-
compound-assignment shape from `doith()`, a bare assignment chained
with another, and a general expression statement (a discarded
`sizeof()` call) chained with an assignment.

**Bug 6, found live, real, well-evidenced, and correctly left
unfixed: `file_name()` has no real per-clone "#\<id\>" suffix (an
already-documented, deliberate gap -- see its own comment in
`EfunTable.cpp`), and this mudlib's own real
`adm/simul_efun/hiddenp.c` depends on exactly that suffix to work.**
Real `hiddenp(ob)` in TMI-2 is implemented as
`!find_object(file_name(ob))` after a `seteuid(0)` privilege bump --
relying on real `file_name()`'s own per-instance uniqueness (real
`obname` always carries a `"#<clone id>"` suffix, `efuns_main.c`'s
`f_file_name()`) so that `find_object()` can re-resolve *that exact
live instance*, not just "some object compiled from this same file".
This driver's `file_name()` returns the bare program path for every
clone of the same file (confirmed directly, already flagged in its own
comment as a known simplification with "nothing this driver runs yet
depends on telling them apart this way" -- this session found the
first real, live corpus dependency), so `hiddenp()` on *any* cloned,
interactive player body returns 1 (wrongly "hidden") rather than 0.
This cascades into `adm/simul_efun/visible.c`'s own real
`visible(detectee, detector)` (`if (hiddenp(detectee_obj))
detectee_vis = 3;`, beating any non-admin `detector_rank`), which in
turn silently broke real cross-player `tell` ("Tellpartner is not on
TMI2." even while both characters were live, in the same room, and
`find_player()` itself correctly found the target -- confirmed
directly via `eval` once Bug 5's fix made `eval` usable again) and
`who`'s own `filter_users()` (each of two simultaneously connected
wizard-status characters saw only themselves in `who`, confirmed with
both connections live and `eval return sizeof(users())` independently
confirming the real connection registry itself was correct: it
returned 2, not 1, so this was never a registry bug). **Not fixed.**
Giving `LpcObject` a real, unique per-file clone-id concept, threading
it through `file_name()`'s own suffix, `find_object()`'s own suffixed-
path parsing (so `find_object("/std/user#7")` re-resolves the *same*
instance, not just any object compiled from `/std/user`), and
`base_name()`'s own suffix-stripping (already noted as depending on
this same concept) is a real, cross-cutting change touching object
identity throughout the whole object-manager layer, not a narrow,
single-file fix like the five bugs above -- named here in full, with
its own concrete real corpus impact now confirmed live, rather than
attempted piecemeal. `wizardp()`/`find_player()`/`users()` themselves
were all independently confirmed correct via the same `eval` probing
session, isolating the fault to `file_name()`/`hiddenp()` specifically
rather than guessed.

**Live verification, over real TCP connections (Python socket
client, as before), several full runs, including two simultaneous
connections for cross-player tests:** `wield`/`unwield`,
`equip`/`unequip` (a sword and a suit of chainmail) all worked
cleanly both directions; `say`, `emote`, `shout` all worked cleanly,
including a real ask/exclaim `say` variant (trailing `?`/`!`); real
combat via `kill` against a cloned `/obj/orc`, after both fixes above,
ran a full real fight end to end with zero further errors -- real
hit/miss resolution both directions ("You stab at Orc with your sword
but you miss." / "Orc swipes at you with hir paws and does light
damage."), real damage messages, and a real kill ("I don't see that
here." / "The combat is over." once the orc died); a `score` call
partway through still hits the already-known, already-tracked `%-`/
`%=` sprintf column-mode gap (row 3.9's own scoped-out limitation, not
re-litigated here). `help wield`'s own trailing literal `"1"` after
its help banner (`_wield.c`'s own `help()` both `write()`s its usage
text directly *and* returns int `1`, which the driver's help daemon
then string-concatenates onto its own banner text, real LPC's own
int-to-string coercion) was checked and confirmed to be real,
faithful mudlib-content behavior, not a driver bug -- `_wield.c`'s own
`help()` is simply inconsistent about whether it writes or returns its
help text, unrelated to this driver.

Full clean rebuild and full suite re-run after every fix in this list
(6 total edits across `CodeGen.cpp`, `Scheduler.cpp`, `EfunTable.cpp`
twice, and `Parser.cpp`/`Parser.hpp`), 849 tests passing throughout by
the end (up from 844), zero regressions at any point. Test character
files created during live verification (`combattest`, `combatterb`,
`tellpartner`, both `data/std/user/` and `data/std/connection/`
entries) deleted afterward, matching this project's own established
cleanup precedent.

Staged with `git add` only, per this project's own standing rule; not
committed.

**2026-09-02 (a further session, same day): TMI-2 boot, row 3.8, real
feature-complete boot and gameplay confirmed. Login, character
creation, look, inventory, and movement between real rooms all
verified over a real TCP connection. 11 real, narrow driver bugs found
and fixed, same rigor as row 3.9's AetherMUD pass -- real citation,
real fix, real regression test for each. 844 tests passing (up from
839), zero regressions at every step. Row 3.8 marked `[x]`.**

**Setup.** `temp/tmi2_fluffos_v3.zip` was already vendored on disk from
a prior session (never previously booted); extracted this session to
`temp/tmi2_fluffos_v3_extracted/` (gitignored under `temp/`, see
CLAUDE.md). `etc/driver_tmi2.cfg` built from that mudlib's own real
`bin/config.tmi2` ("mudlib directory : /mud/tmi2/lib", "master file :
/adm/obj/master", "simulated efun file : /adm/obj/simul_efun",
"include directories : /include", "global include file : <mudlib.h>"),
same derivation discipline as `etc/driver_aethermud.cfg`.

**The 11 real driver fixes, in the order the boot attempt actually hit
them:**

1. **`sscanf()` output arguments restricted to a bare variable name.**
   Real `grammar.y`'s own `sscanf:` production uses `lvalue_list`, not
   a name list -- any indexed lvalue is real, valid syntax. Real corpus:
   `adm/obj/master/access.c`'s own `sscanf(lines[i], "(%s)%s", path,
   lines[i])`, writing its second output straight back into the very
   array element the source string was read from. Fixed in
   `Parser`/`CodeGen`: an indexed output target (`IndexExpr`) is written
   through a hidden compiler-synthesized temp local (the existing
   `SscanfVarSlot` mechanism, unchanged) plus a deferred `IndexAssign`
   sequence emitted right after the `Sscanf` instruction -- `OpCode::
   Sscanf` itself needed no changes at all. Range-form output targets
   (`arr[1..3]`) are rejected as a parse error, not silently misparsed.

2. **`|=`/`&=`/`^=` compound bitwise assignment entirely unlexed.**
   `access[path][name] |= WRITE;` (real corpus: 30 `|=`, 6 `&=`, 2 `^=`
   call sites in this one mudlib alone) lexed as two separate tokens,
   `|` then `=`, so the parser's own compound-assign recognition (which
   checks the *next* token's exact text) never matched, silently
   falling through to plain binary-or parsing and failing on the bare
   `=` it did not expect. `Lexer::lexSymbol()` gained the missing
   two-char cases; both `Parser` compound-op tables (the statement-level
   `IndexAssignStmt` fast path and the unified expression-level
   `AssignExpr`/`IndexAssignExpr` path) gained the three entries; all
   three `CodeGen` combine-op switches gained the three cases, reusing
   the already-implemented `OpCode::BitOr`/`BitAnd`/`BitXor` real
   plain-binary-operator opcodes. Shift operators (`<<`/`>>`/their
   compound forms) are a separate, unevidenced gap -- zero real hits
   anywhere in this same corpus pass, and this driver has no plain
   shift operator at all yet either -- not added.

3. **Hex integer literals (`0x0010`) not lexed past the leading `0`.**
   `adm/simul_efun/vt100.c`'s own real `if (color_mode & 0x0010)` (a
   VT100 bright/bold-attribute bitflag test). `Lexer::lexNumber()`
   consumed only the leading `0`, then re-entered the tokenizer at `x`,
   producing a stray `Ident` token ("x0010") the parser could not use
   ("expected \")\" in if condition ... got \"x0010\""). Fixed with a
   dedicated hex branch in `lexNumber()` (kept in the token's own text,
   not stripped, matching how the float `.` case already works);
   `Parser`'s `IntLiteral` construction detects the `0x`/`0X` prefix and
   parses with `std::stoll(raw, nullptr, 16)`, every non-hex literal
   completely unaffected (still plain `std::stoll(raw)`, base 10).
   Octal is a deliberately separate, unevidenced concern -- no octal
   literal appears anywhere in this same corpus, and switching the
   general integer-literal path to base-0 auto-detection would have
   been a real, silent behavior change for any existing leading-zero
   decimal literal (`010` becoming 8 instead of 10) with no real
   evidence forcing it -- not made.

4. **A real off-by-one in single-character string indexing.** Real
   `interpret.c`'s own `F_INDEX` `T_STRING` case: `if ((i >
   SVALUE_STRLEN(sp)) || (i < 0)) error(...)` -- strictly greater than,
   not `>=`, so indexing exactly at a string's own length (its implicit
   NUL terminator) is real, defined, non-throwing behavior reading back
   0. This driver's own `VM.cpp` used `>=`, throwing exactly there
   instead. Broke the ubiquitous real LPC idiom `if (lines[i][0] == '#'
   || lines[i] == "") continue;` (`adm/obj/master/groups.c` and
   `access.c` both) for any empty exploded line: `lines[i][0]` on an
   empty string needs to read back 0, not throw, or the `""` comparison
   never even runs. Fixed by relaxing the bound to `>` -- a real
   `std::string::operator[](size())` is already guaranteed by the C++
   standard to return a null character, so no special-casing was needed
   beyond the bound itself.

5. **No trailing comma allowed in a call's argument list.** Real
   `grammar.y`'s own `expr_list2 ','` production explicitly allows one,
   dropped with no extra element added -- the same real allowance array/
   mapping literals already had here. Real corpus:
   `adm/daemons/newuserd.c`'s own `body->set("PATH", AUTO_WIZHOOD);`,
   where `AUTO_WIZHOOD` is a real, deliberately valueless flag `#define`
   (`config.h`'s own "The AUTO_WIZHOOD define causes all those [logging
   in] as new users to be [granted wizard status]", a pure boolean
   `#ifdef`-style flag) -- real cpp's own correct expansion is literally
   `body->set("PATH", );`, previously rejected outright ("expected
   expression ... got ')'"). Fixed in `Parser::parseArgList()`.

6. **Switch `case A..B:` range labels.** Previously a deliberate
   `NotImplementedError` -- "nothing in this mudlib uses them", true of
   the bundled `mudlib/` at the time, not of TMI-2: `std/user.c`'s own
   real level-tier ladder (`case 2..3:`, `case 4..6:`, ...). Real
   `grammar.y`'s own `L_CASE case_label L_RANGE case_label ':'`
   production. `CaseLabel` gained a `rangeEnd` field; `CodeGen::
   emitSwitchStmt()` builds the range check as the same short-circuit
   `(subject>=low) && (subject<=high)` shape `emitLogicalExpr()`'s own
   AND branch already uses, written directly against the subject's
   local slot -- reusing `OpCode::Gte`/`Lte`/`Not`/`JumpIfFalse`/`Dup`/
   `Pop`, no VM opcode changes at all. The open-ended real grammar forms
   (`case A..:`, `case ..B:`) are a separate, still-unevidenced gap --
   neither appears anywhere in this same corpus pass -- and stay
   unimplemented.

7. **Unary `~` (bitwise NOT) entirely unlexed.** `std/user/bitflags.c`'s
   own real `bitflags &= ~(n);` bitmask-clearing idiom threw
   "unrecognized character '~'" outright -- there was no lexer case for
   a bare `~` at all. Real `grammar.y`'s own `'~' expr0` at the same
   `%right L_NOT '~'` precedence tier as unary `!`. Added end to end:
   `UnaryOp::BitNot` (Ast.hpp), `OpCode::BitNot` (Bytecode.hpp), a
   right-associative `~` prefix in `Parser::parseUnary()` (matching
   `!`/`-`'s own shape), `CodeGen`'s emission case, and a new
   `OpCode::BitNot` VM handler matching real `interpret.c`'s own
   `F_COMPL` exactly, including its literal "Bad argument to ~" message
   for a non-int operand. 4 new regression-test assertions, hand-
   computed from the two's-complement identity `~x == -x-1`, not read
   back from this driver: `~5 == -6`, `~(-3) == 2`, `~~7 == 7` (double
   negation), plus the non-int throw.

8. **A nested absolute `#include` was never resolved.** Real cpp has no
   concept of a mudlib root, so a quoted `#include "/..."` fails
   outright ("No such file or directory") unless rewritten first; this
   driver's own `rewriteAbsoluteIncludes()` already did that, but only
   ever once, on the *outermost* file's own raw text, before cpp ever
   ran. A file reached transitively via cpp's own recursive `#include`
   expansion (because *its* own absolute include had already been
   correctly rewritten and resolved) never had its own absolute
   includes rewritten at all. Real corpus:
   `std/object/sec_ob.c`'s own `#include "/std/object/prop.c"`
   (resolved fine) itself contains `#include
   "/std/object/prop_logic.c"` (never touched, cpp choked on it as a
   literal OS-root-absolute path). Confirmed directly, not assumed: a
   leading-`/` quoted include is resolved as a raw absolute filesystem
   path by real cpp unconditionally, no `-I` search path applies to it,
   and GCC's own `--sysroot` flag does not redirect quote-form includes
   either (tested empirically before ruling it out). Fixed by making
   `rewriteAbsoluteIncludes()` recursive: for each absolute quoted
   include whose real target file can actually be read, its content is
   spliced directly in place (masked for `#'` the same way the outer
   file already is, then recursed into for its own further absolute
   includes), with a real-cycle guard (`activeIncludes`) and a depth cap
   as a pure safety net. A target that cannot be read falls back to the
   original path-text rewrite, so real cpp's own "No such file or
   directory" still surfaces for a genuinely broken include. New
   regression test: two levels of absolute-include nesting, matching
   the real corpus shape exactly. The existing 50-real-include
   AetherMUD test (`testAbsoluteIncludePathResolvesAgainstMudlibRoot`
   and its own live corpus, `secure/SimulEfun/SimulEfun.c`) re-ran
   unchanged throughout, confirming the switch from path-rewrite to
   content-splice is behaviorally equivalent for the single-level case.

9. **`ObjectManager::normalizeFilename()` assumed every real LPC
   pathname already carried a leading `/`.** Real LPC object pathnames
   are mudlib-root-relative whether or not the string itself has one --
   `inherit "std/room";` and `inherit "/std/room";` name the same real
   object. Real corpus: `std/object/sec_ob.c`'s own `inherit
   "std/object/ob_logic";` (no leading slash; the real file sits
   directly at that mudlib-root-relative path). This driver's own
   `compile()`/`loadObject()`/`cloneObject()`/etc. all build a real
   filesystem path as `config_.mudlibRoot() + filename + ".c"`,
   assuming a leading `/` was already present -- true for every
   absolute-written path this driver's own corpus had exercised before
   now, silently wrong for a relative one ("...tmi2_fluffos_v3/lib" +
   "std/object/ob_logic.c" = "...tmi2_fluffos_v3/libstd/object/
   ob_logic.c", confirmed live as a real "source file not found").
   Fixed by having `normalizeFilename()` ensure exactly one leading `/`
   (stripping one first if already present, not blindly prepending a
   second one). New regression test: an `inherit` target with no
   leading slash resolves to the exact same real object as the
   leading-slash form.

10. **`deep_inherit_list()`/`shallow_inherit_list()`/`inherit_list()`'s
    own leading-slash convention was backwards.** A prior session's own
    comment claimed "no leading slash" from a different corpus's own
    `member_array("std/armour.c", deep_inherit_list(ob))`-style call
    sites -- never actually checked against real driver source, and
    wrong: real `array.c`'s own `deep_inherit_list()`/`inherit_list()`
    both do `ret->item[il].u.string = add_slash(pr->filename);` for
    every entry, and real `add_slash()` (`interpret.c`) unconditionally
    prepends `/`. Found live against a real *third* mudlib corpus:
    `adm/simul_efun/overrides.c`'s own real security-gated `exec()`
    simul_efun override (this same mudlib defines its own `exec()`,
    shadowing the raw efun, exactly as real LPC's own function-
    resolution order allows) does `member_array("/std/body.c",
    deep_inherit_list(to_obj)) == -1` -- *with* a leading slash, real
    add_slash()-matching, not that other corpus's own convention. This
    driver's own prior "strip the leading slash" behavior meant this
    real, load-bearing security gate (verifying a new character's own
    body object genuinely inherits `/std/body.c` before granting it the
    interactive connection) could never find a match, always denying.
    This alone was the root cause of "Error connecting to your body..."
    on every single character creation attempt, cascading moments later
    into a real call_other-on-a-destructed-object crash in
    `adm/daemons/newuserd.c`'s own `get_real_name()` (`body_ob` had
    already been `remove()`d by the denied `exec()`'s own real cleanup
    path). Debugged by adding temporary `std::cerr` tracing directly
    inside the real `exec()` efun (reverted immediately once the actual
    root cause -- the efun was never even reached, `overrides.c`'s own
    simul_efun shadowed it first -- was confirmed), not guessed. Fixed
    by having `normalizeInheritPath()` ensure exactly one leading `/`
    instead of stripping one. Existing regression test
    (`testShallowAndDeepInheritListWalkARealThreeLevelChain`) updated in
    place to assert the correct, now-verified-against-real-source
    format.

11. **`VM::resolveMudlibPath()` had the identical "assumes a leading
    /" bug, for every file efun.** Real corpus, found live a *third*
    time in this same pass: `adm/daemons/ga_server.c`'s own `#define
    GLOBAL_ALIASES "adm/etc/global_aliases"` (no leading slash) fed
    straight into `read_file(GLOBAL_ALIASES)`. `resolveMudlibPath()`
    concatenated `config_.mudlibRoot() + lpcPath` with no separator
    guarantee, so `read_file()` silently returned falsy for this
    relative-form path, cascading into `ga_server`'s own `create()`
    failing ("explode: expected (string, string) arguments" -- its
    first argument was the falsy `read_file()` result), which in turn
    left every command routed through `std/user.c`'s own `do_xverb()`
    failing with "call_other() couldn't find object". This alone was
    the root cause of `look` and `inventory` both failing immediately
    after a successful login. Fixed the same way as (9): ensure exactly
    one leading `/` rather than assuming one. New regression test: a
    relative-form `read_file`/`write_file` path round-trips identically
    to the existing leading-slash test.

**Real, well-evidenced issues found and correctly left unfixed.**
`include/login.h`/`include/tsh.h`/`adm/simul_efun/overrides.c` all use
`#ifdef 0` as a "commented out" idiom -- invalid per the C standard
(`#ifdef` requires an identifier; `0` is a pp-number token, not one),
rejected by any strict modern cpp with "macro names must be
identifiers". Real 1990s-era content meeting a modern system cpp: real
modern FluffOS booting this exact same file against this exact same
system cpp would hit the identical error, not a driver defect.
`adm/simul_efun/iwrap.c`'s own `str[0..D_IN]` is a separate, genuine C
preprocessor "pp-number" tokenization gotcha: `0..D_IN` greedily
lexes as one single opaque pp-number token under the real C standard's
own pp-number grammar (a digit followed by any run of digits,
identifier-nondigits, or `.`), so it is never macro-expanded at all --
also real cpp behavior, confirmed by direct empirical test (`cpp -x c`
on the isolated fragment), not this driver's. `/cmds/std/_score::
cmd_score()`'s own sprintf usage hits the `%=` column-mode gap row
3.9's AetherMUD pass already found and explicitly scoped out -- not
re-litigated here, same known limitation. The real Intermud3 network
daemon (`adm/daemons/network/I3`) cannot reach a real external gateway
server from this sandboxed test environment -- a genuine environmental
constraint (no real internet access to whatever real i3 router TMI-2
targets), not a driver bug -- cascading into `adm/daemons/channels.c`'s
own I3-channel-list registration on daemon boot. Three of these four
(the two `#ifdef 0` files and the `channels.c` I3 line) were given a
minimal, clearly-labeled, non-tracked local patch purely to keep the
live verification going past them -- `temp/` is gitignored and never
committed, so this is not a driver change and not a claim that real
FluffOS tolerates any of them; it is exactly what a real admin running
this mudlib locally today would also have to do.

**Live verification, over a real TCP connection (Python socket
client), through several full runs:** account/character creation
(name, confirmation, password, gender, race, email, real name) all the
way through "[You have been granted automatic wizard status]" and into
the real starting room ("This is the famous TMI-2 quad reincarnated.");
`look` re-showing the same real room description; `inventory` showing
"You are carrying 0 objects."; `north` moving to a real, different room
("This is the driver room..."), `look` there confirming it, `south`
moving back. `score` also worked partway (real HP/SP display) before
hitting the already-known, already-tracked `%=` sprintf gap. Full clean
rebuild and full suite re-run after every single fix in this list, 844
passing throughout, zero regressions at any point.

`ROADMAP.md` row 3.8 marked `[x]` with the full fix list and citations
in its own cell. The other two named row 3.8 candidates (LPUniversity,
LIL) remain open if a further third-party-mudlib boot is wanted next.

Staged with `git add` only, per this project's own standing rule; not
committed.

**2026-09-02 (a further session, same day): row 3.3 vs row 3.4 scoped
and compared, TELOPT_TTYPE negotiation built (row 3.4's first slice).
With the uid/security cluster (rows 3.1/3.2) closed, this session
scoped both remaining-named Phase 3 candidates against real source
before writing any code, picked the smaller and safer of the two, and
built it. 839 tests passing (up from 833). Row 3.4 now `[ ]
(partial)`; row 3.3 unchanged (not picked up, see its own finding
below).**

**Scoping row 3.3 (generational GC).** `src/gc/instruct.md`'s own
premise -- "replace the current shared_ptr-everywhere memory model with
a generational garbage collector" -- was checked against real source
directly rather than trusted, and does not hold up. Real FluffOS 2.9:
grepped the whole vendored tree for anything GC-shaped
(`mark_and_sweep`, `garbage_collect`, a `"gc"` func_spec entry); the one
hit, `reclaim.c`, is not a garbage collector at all -- its own header
comment says so plainly ("loops through all variables in all objects
looking for the possibility of freeing up destructed objects"), and its
body (`check_svalue()`/`gc_mapping()`) only nulls out lingering
`T_OBJECT` references to already-destructed objects, a periodic
scrubber layered on top of what is otherwise a plain reference-counted
driver (`object.h`'s own `ob->ref`, `array.h`'s own `array->ref` --
exactly the strategy `shared_ptr` already gives this driver today).
Real LDMud does have a genuine collector, `temp/ldmud/src/gcollect.c`,
confirmed by reading it directly -- but it is a diagnostic/leak-
recovery mark-and-sweep pass triggered by an explicit
`garbage_collection()` efun or a memory-pressure threshold, layered on
top of still-refcounted memory, not a continuously-running generational
collector that replaces refcounting. **Finding:** row 3.3's own real
first slice, if picked up, is porting LDMud's leak-detection/-recovery
mechanism onto the existing `shared_ptr` model, not the large from-
scratch generational rewrite `src/gc/instruct.md` describes (still
explicitly, by the ROADMAP's own words, "the single most invasive
change in the roadmap" even under this narrower framing, since it
still touches the object cache, `InteractiveRegistry`, `Scheduler`,
and `Server` all at once per that same instruct.md). That rescoping is
its own real investigation and was not done this session -- flagging
it plainly rather than starting an invasive change on an unverified
premise.

**Scoping row 3.4 (telnet options + GMCP/MSDP/MSSP/MTTS/MXP).** Read
real `comm.c` directly for each of the five named protocols rather than
assuming the row title's own bundling reflects real driver parity.
GMCP, MSDP, MSSP: zero real source anywhere in the vendored FluffOS 2.9
(`ds2.08`) tree -- grepped case-insensitively across every `.c`/`.h`
file and every `packages/` file, nothing; these are later FluffOS
additions or third-party protocol extensions that postdate this
project's vendored snapshot, not a porting target at all. MXP: real
source exists and is small (`comm.c`'s own `TELOPT_MXP` handling,
`APPLY_MXP_ENABLE`/`APPLY_MXP_TAG`, `f_has_mxp()`), a plausible future
slice, not picked up this session. TTYPE/MTTS: real source exists and
is the smallest, safest, most self-contained real target available
anywhere in this Phase 3 cluster -- confirmed by reading `comm.c`'s
`new_user()`, `TS_WILL`, `TS_SB`, and the two real efun bodies
(`f_request_term_type()`/`f_start_request_term_type()`) directly.
Already flagged as a known gap by this driver's own prior code
(`Server.cpp`'s own comment: "real new_user() also sends IAC DO TTYPE
and IAC DO MXP here, neither of which anything in this driver
processes yet"), touches only `src/net`, and carries no VM/memory-
model risk whatsoever -- the clear smaller-and-safer choice against row
3.3's now-confirmed large, still-unscoped invasiveness. Picked.

**Real source, read directly.** `temp/reference/fluffos-2.9-ds2.08/
comm.c:1693-1704` (`new_user()`: sends `IAC DO TTYPE` before `IAC DO
NAWS` before `IAC DO MXP`, unprompted, at connection setup -- this
driver previously sent only the NAWS byte sequence, confirmed by
reading `Server.cpp`'s own prior code, not just its comment).
`comm.c:808-838` (`TS_WILL`'s own switch: `TELOPT_TTYPE` answers with
the real `telnet_term_query[]` probe, `IAC SB TTYPE SEND IAC SE` -- and
this driver's own prior `handleNegotiation()` had a real, previously-
unfixed bug here: WILL TTYPE fell through to the default branch and was
wrongly refused with `IAC DONT TTYPE`, confirmed by reading the prior
code directly before touching it, not assumed from the row's own
"gap" framing). `comm.c:1078-1083` (`TS_SB`'s own `TELOPT_TTYPE` case:
a `TELQUAL_IS(0)` response's payload, from index 2 onward, is handed to
the mudlib via `apply(APPLY_TERMINAL_TYPE, ip->ob, 1, ORIGIN_DRIVER)`;
any other TELQUAL byte -- a client echoing `TELQUAL_SEND(1)` back,
say -- carries no string and is silently ignored). `comm.c:2894-2904`
(`f_request_term_type()`: resends the `telnet_term_query[]` probe;
`f_start_request_term_type()`: resends the bare `telnet_do_ttype[]`,
`IAC DO TTYPE`). `applies.h:30` confirms the real apply name,
`terminal_type`. `telnet.h:85,120-121` confirms the real option/qualifier
byte values: `TELOPT_TTYPE = 24`, `TELQUAL_IS = 0`, `TELQUAL_SEND = 1`.

**Real finding that reshaped this row's own scope.** Real FluffOS's
entire driver-level MTTS mechanism, confirmed by reading `comm.c` in
full rather than assuming the row title's own five-protocol bundling
implies five roughly-equal driver-side features, is exactly the three
primitives above (`terminal_type()` apply, `request_term_type()`,
`start_request_term_type()`) -- there is no round-counting state, no
previous-answer comparison, and no bitmask table anywhere in the real
driver. The actual Mud Terminal Type Standard's own multi-round
convention (call `request_term_type()` again, compare the new
`terminal_type()` value against the last one, stop once it repeats or
a third round yields a client-sent `"MTTS <bitmask>"` string) is
genuinely mudlib-side in real FluffOS -- the mudlib's own
`terminal_type()` handler drives the whole loop itself. `src/proto/
instruct.md`'s own `MttsHandler` design (a driver-side `query_client_
flags()` efun parsing the bitmask internally) does not match this real
architecture -- noted here rather than built, the same "real source
first, flag what has none plainly" discipline row 3.2's `call_other`
capability-grant finding already established for this Phase 3 area.

**Built.** `Connection::requestTerminalType()`/`startRequestTerminalType()`
(`Connection.cpp`, mirroring `requestWindowSize()`'s own existing real-
citation shape exactly), `handleNegotiation()`'s new `WILL TTYPE`
branch (fixing the DONT-refusal bug above -- a client volunteering WILL
TTYPE unprompted, e.g. outside this driver's own `Server::
onNewConnection()` DO-TTYPE-first flow, now gets the real SB TTYPE SEND
probe instead of a wrong refusal), `handleSubnegotiation()`'s new
`TELOPT_TTYPE` case (`terminalType_`/`terminalTypeUpdated_`, the same
one-shot-flag shape `terminalWidth_`/`windowSizeUpdated_` already use,
including the real "fires every time, not only on a changed value"
behavior), `Server::onNewConnection()`'s corrected send order (DO TTYPE
now sent before DO NAWS, matching real `new_user()` exactly; DO MXP
still not sent, MXP stays fully out of this slice), `Server::
handleConnection()`'s new `terminal_type()` apply-firing block
(mirroring the existing `window_size()` block's own shape exactly), and
three efuns in `EfunTable.cpp`: `request_term_type()`,
`start_request_term_type()` (both real, matching the two real
`f_request_term_type()`/`f_start_request_term_type()` bodies), and
`query_terminal_type(object)` (a driver-added pull-based convenience
over the same real push-based data, explicitly not a real FluffOS
efun -- grepped `func_spec.c`/`efun_defs.c`/`applies_table.c` directly,
zero hits -- the same precedent `query_screen_width()`/
`query_screen_height()` already set for NAWS).

**6 new regression tests (839 total, up from 833):**
`testTelnetWillTtypeIsAnsweredWithSbTtypeSendProbeNotRefused` (the real
bug fix itself -- WILL TTYPE now gets the probe, not a DONT refusal),
`testTtypeSubnegotiationUpdatesTerminalTypeAndFlagOnce` (SB TTYPE IS
parsing plus the one-shot flag's consume-once contract, mirroring
`testWindowSizeUpdateFlagSetOnNawsAndConsumedOnce`'s own shape),
`testTtypeSubnegotiationWithNonIsQualByteIsIgnored` (the real `!ip->
sb_buf[1]` TELQUAL guard specifically, not folded into the IS-parsing
test), `testRequestTermTypeSendsIacSbTtypeSendAndIsNoOpWithoutInteractiveCommandGiver`
and `testStartRequestTermTypeSendsIacDoTtypeAndIsNoOpWithoutInteractiveCommandGiver`
(both new efuns' real byte sequences and their silent no-op without an
interactive command_giver, mirroring `testRequestTermSizeSends...`'s
own established shape), `testQueryTerminalTypeReturnsNegotiatedValueAndThrowsWhenNotInteractive`
(the driver-added convenience's value read plus its not-interactive
throw, mirroring `testQueryScreenWidthAndHeightReturnNegotiatedValues`).
Full clean rebuild, full suite re-run, 839 passing, zero regressions
among the pre-existing 833. Live re-boot of the bundled `mudlib/`
(`./build/amlp etc/driver.cfg`, 5 seconds): stayed silent and running
the whole time, no compile errors, no crash.

**Row 3.4 marked `[ ] (partial)`.** Real driver-level TTYPE/MTTS
negotiation is done; MXP (real source exists, not yet ported), GMCP/
MSDP/MSSP (zero real source in the vendored tree, would be external-
spec work rather than a port), and the MTTS bitmask-decoding
convenience `src/proto/instruct.md` originally wanted (deliberately not
built, per the finding above -- real architecture puts that decode
entirely on the mudlib side) all remain open in this same row.

`ROADMAP.md` row 3.4 updated in place; row 3.3 untouched (a finding
recorded in its own row 3.4 cell and here, not a row 3.3 edit, since
nothing was built or ruled out for row 3.3 itself, only rescoped by
finding).

Staged with `git add` only, per this project's own standing rule; not
committed.

**2026-09-02: `legal_path()` `..`/`#` traversal check, closing row 3.2
(ROADMAP rows 3.1 / 3.2). The concrete next cut the prior session's
row 3.2 re-scoping named: `checkValidPath()`'s own comment flagged real
`legal_path()`'s `..`/`#`-character sanity check as deliberately not
replicated. Built it, dialect-faithful (FluffOS silent deny, LDMud
catchable runtime error). 833 tests passing (up from 827). Row 3.2 now
`[x]`; row 3.1 unchanged. This closes the real driver-level
uid/security cluster (rows 3.1 + 3.2) -- see below.**

**Why this, and why now.** Row 3.2's own 2026-09-01 re-scoping already
identified this as the one genuine remaining gap once items (1) and (2)
were done in row 3.1's slice 2: `checkValidPath()` already ran the
`valid_write`/`valid_read` master apply, but never followed it with
`legal_path()`, the second half of what real `check_valid_path()` does.
Picked up directly rather than re-searching the cluster, per that
session's own explicit framing of it as "the concrete smallest next cut
for this row if picked up."

**Real source, read directly, both dialects.** FluffOS 2.9
`temp/reference/fluffos-2.9-ds2.08/file.c:295-334`'s own `legal_path()`:
rejects a leading `/`, any `#` anywhere in the path (own comment,
verbatim: "disallowing # seems the easiest way to solve a bug involving
loading files containing that character" -- real FluffOS filenames use a
trailing `#<clone-number>` suffix for cloned-object identity, so a raw
`#` reaching a file efun risks colliding with or forging that internal
naming scheme), and, via its own real pointer-walk over `strstr(p,
"/.")`, any `..` or bare `.` path component -- including a genuine quirk
that is easy to lose in a from-scratch reimplementation: a *leading*
`./` is illegal too, not only `../`, because real code's own `if (p[1]
== '/' || p[1] == '\0') return 0;` fires for any single-dot component,
not only a double-dot one. `file.c:705-750`'s own `check_valid_path()`
confirmed the full real pipeline directly: push `(path, call_object,
call_fun)`, apply `valid_write`/`valid_read`, a `0` return denies, a
string return rewrites the path, strip one leading `/`, an all-slash
path becomes `.`, THEN `legal_path()` runs as the final gate -- and
there is no second per-domain layer anywhere in the driver, the
domain-to-path-prefix mapping lives entirely in the mudlib master's own
`valid_write()` body (`secure/daemon/master.c`-shaped). LDMud's
equivalent, also read directly rather than assumed to share FluffOS's
algorithm behind the shared name: `temp/ldmud/src/simulate.c:1734-1776`
(`check_no_parentdirs()` + `legal_path()`): rejects a leading `/`, an
embedded space unless `allow_filename_spaces` (`main.c:139`, defaults
`MY_FALSE`, a command-line-only toggle this driver has no config-key
equivalent for), and an anchored `..` path component -- narrower than
FluffOS's own version in two real ways: no `#` check at all (grepped the
whole file, never mentioned), and a bare embedded `/./ ` is left alone,
not rejected. `simulate.c:3752-3851`'s own `check_valid_path()` runs the
same apply-then-strip-then-`legal_path()` shape as FluffOS, but a real,
material divergence on failure: an illegal path throws a catchable
runtime error (`:3846-3849`'s own `errorf("Illegal path '%s' for %s() by
%s\n", ...)`), not FluffOS's silent `return 0`.

**Real corpus evidence this is not academic.** This driver's own
`resolveMudlibPath()` (`VM.cpp`) is a bare `mudlibRoot + lpcPath` string
concatenation with no sandboxing of its own, matching real FluffOS's own
disk-I/O shape exactly. Every vendored mudlib corpus this project tracks
builds file-efun paths from ordinary LPC string concatenation, much of
it player-influenced -- a login/account-creation flow composing a save
path from a player-chosen name (`"/save/" + name + ".o"`-shaped) is the
representative real case. Before this session, a crafted
`../../../etc/passwd`-shaped (or `#`-embedding) path reaching any of
this driver's already-gated file efuns would walk straight past
`mudlibRoot` on real disk I/O the moment the permissive-by-default
master-apply step passed it through unmodified, which it does for every
master that never defines `valid_write`/`valid_read` at all -- this
driver's own bundled `mudlib/` included.

**Built.** `legalPathFluffos()` and `legalPathLdmud()` (`EfunTable.cpp`,
anonymous namespace, immediately above `checkValidPath()`), each a
bounds-checked, line-by-line port of its real body -- a bounds-checked
`at()` lambda stands in for C's own null-terminator read where the real
pointer walk reads one character past a match, otherwise the same
`p[0]`/`p[1]`/`p[2]` branching -- not a from-scratch "block dot-dot-
slash" reimplementation, which is why the real quirks above (FluffOS's
own leading-`./`-illegal behavior, LDMud's own leave-`/./`-alone
behavior) survive intact rather than being designed away. `checkValidPath()`
now runs the dialect-appropriate one of the two, after the existing
master-apply gate, against the approved path with one leading `/`
stripped for the check only (`checkPath`, local to the check; the value
`checkValidPath()` actually returns keeps its leading `/` unchanged,
this driver's own established `resolveMudlibPath()` convention, not
real code's own separate "return without a leading '/'" contract, since
changing that would touch every existing file-efun call site's own path
handling for no reason this row needs). Denies silently under FluffOS
(`std::nullopt`, indistinguishable from a master-apply deny to every
existing call site, matching real `file.c:750`); throws `LpcRuntimeError`
with the real `errorf()`-format message under LDMud, a genuine dialect
divergence ported faithfully rather than flattened to one shared
behavior. **Named divergence:** a null caller (a shape real LDMud's own
`fatal("Illegal caller for check_valid_path.\n")` guard would not
permit, but this driver already reaches this point for calls with no
current object elsewhere in its own existing permissive-default
precedent) substitutes the literal string `"?"` for the caller name in
the LDMud error message rather than crashing.

**6 new regression tests (833 total, up from 827):**
`testCheckValidPathRejectsParentDirectoryTraversalFluffos` (`/../foo`
denied, confirmed genuinely never written outside `mudlibRoot`, not just
that the return value was 0), `testCheckValidPathRejectsEmbeddedHashFluffos`,
`testCheckValidPathRejectsLeadingDotSlashFluffos` (the real leading-`./`
quirk specifically, not folded into the `..` test), `testCheckValidPathAcceptsOrdinaryDottedFilenameFluffos`
(the false-positive check -- an ordinary versioned-save-style dotted
filename still round-trips write then read, proving the new gate does
not over-reject the common case), `testCheckValidPathLdmudThrowsCatchableRuntimeErrorOnTraversal`
(verified through an actual LPC-level `catch()`, not only a native
try/catch around `callFunction()`, proving it is a genuine catchable LPC
error and checking the message shape matches real `errorf()`'s own
format), `testCheckValidPathLdmudRejectsSpaceButAllowsHash` (the two
dialects' own real, opposite-direction divergence exercised in one
test: LDMud denies a space FluffOS would allow through, and allows a
`#` FluffOS would deny). Full clean rebuild, full suite re-run, 833
passing, zero regressions among the pre-existing 827. Live re-boot of
the bundled `mudlib/` (`./build/amlp etc/driver.cfg`, 5 seconds): stayed
silent and running the whole time, no compile errors, no crash --
confirms the new gate does not break any real file access this mudlib's
own boot or master.c path already relies on.

**Row 3.2 marked `[x]`.** The `call_other` capability-grant half named
in row 3.2's own title (an object may only `call_other()` into another
object's functions it has been granted access to) is explicitly *not*
part of what closes this row: re-confirmed this session that it has no
real FluffOS analog in the vendored 2.9 source (nothing new since the
2026-09-01 finding) -- it is a DGD-style capability model, a genuinely
novel design item, not a port, and stays deliberately deferred until and
unless it is explicitly wanted. Marking the row `[x]` reflects that
every item it named with an actual real source citation is now built.

**This closes the real driver-level uid/security cluster.** As of this
session, rows 3.1 and 3.2 together have now had every one of their own
named items read directly against real FluffOS 2.9 and/or LDMud source
and either built or explicitly ruled out as having no real driver-level
source to port (uid/gid group membership, row 3.1; `call_other`
capability grants, row 3.2). There is nothing further in this specific
cluster with real driver-level source left to search for. The next real
target in this area, if wanted, is genuinely novel design work (the
`call_other` capability grants above, or a from-scratch per-domain
filesystem jail beyond what real `valid_write()`-in-the-mudlib already
provides), not a further FluffOS/LDMud port. Recommend picking up a
different roadmap row instead -- 3.3 (generational GC) or 3.4 (full
telnet option negotiation) are the next `[ ]` rows under this same Phase
3 with real, concrete scope already written up.

`ROADMAP.md` row 3.2 updated in place; row 3.1 untouched.

Staged with `git add` only, per this project's own standing rule; not
committed.

**2026-09-01 (a further session, same day): uid / euid object trust
model, slice 3 (ROADMAP rows 3.1 / 3.2). The domain-trust traversal half
of row 3.1's item (c): the `AUTO_TRUST_BACKBONE` middle branch of real
`give_uid_to_object()`, exposed as a default-off `auto_trust_backbone`
config key. Also a plain finding, not a build: real uid/gid *group
membership* has no driver-level source in either the vendored FluffOS
2.9 or LDMud, so it is deliberately not invented. Row 3.2's item (3)
re-scoped after reading real `check_valid_path()` directly. 827 tests
passing (up from 825). Rows 3.1 and 3.2 cells both updated; row 3.1
stays `[x] (partial)`, row 3.2 stays `[ ]`.**

**Why this, and why now.** Slice 1 built the uid/euid data model and the
four uid efuns; slice 2 wired the two places that data first gates
behavior (`clone_object()` guard, `valid_read`/`valid_write` uid arg).
Row 3.1's title still named "uid/gid ... hierarchy" and its "Still not
built" clause named "(c) real uid/gid group membership and the domain
trust traversal". This slice closes the part of (c) that has a real
source to port and states plainly that the rest does not.

**Finding: no driver-level group membership to port.** Read directly:

  - **FluffOS 2.9** (`temp/reference/fluffos-2.9-ds2.08/`): `packages/
    uids.c` carries only `userid_t {char *name;}`, `root_uid`,
    `backbone_uid`. No gid struct, no group list, no group-membership
    call anywhere. `grep -n "gid\|group_member\|->groups"` across
    `*.c`/`*.h`/`packages/` returns only OS-level `st_gid` in
    `file.c:1181` (stat output) and `portbind.c`'s `setgid()` (the
    privileged port-bind helper). `packages/uids_spec.c` exposes only
    `getuid`/`geteuid`/`seteuid`/`export_uid`.
  - **LDMud** (`temp/ldmud/`): `src/func_spec:665-668` exposes only
    `geteuid`/`getuid`/`creator`. `doc/efun/getuid` describes the uid as
    "the name of the wizard **or domain**" but that is a mudlib-level
    label on one opaque string, not a driver group set. `grep -rln
    "\bgid\b" src/*.c` hits only `pkg-openssl.c`.

  Group membership in real LPMud security is entirely a mudlib concept
  (the master's own `valid_read`/`valid_write`/`creator_file` bodies map
  one uid string to whatever domain/group policy that mudlib wants).
  There is nothing in either driver to port, so per this project's
  port-not-invent discipline it is not built.

**Cited real source for the domain-trust traversal, read directly.**

  - **The branch:** `temp/reference/fluffos-2.9-ds2.08/simulate.c:
    178-186`, inside `give_uid_to_object()`, between the "same uid as the
    loader" branch (`:166-176`) and the "another wizard ... can't be
    trusted" fallback (`:196-204`):
    `#ifdef AUTO_TRUST_BACKBONE / if (strcmp(backbone_uid->name,
    creator_name) == 0) { ob->uid = current_object->euid; ob->euid =
    current_object->euid; return 1; } / #endif`. An object whose
    `creator_file()` is the backbone uid is trusted and runs with the
    loader's effective identity.
  - **Why slice 1 skipped it:** `AUTO_TRUST_BACKBONE` is `#undef` in the
    vendored reference build, `local_options:671-675` (same as
    `options.h:677`).
  - **Corpus evidence for making it opt-in:** `#define
    AUTO_TRUST_BACKBONE` in three of the vendored FluffOS mudlib option
    variants: `temp/reference/fluffos-2.9-ds2.08/local_options.lima:574`,
    `local_options.tmi2`, `local_options.merentha`. LIMA and TMI2 are
    both named boot candidates on ROADMAP row 3.8.
  - **LDMud divergence:** no driver-level backbone or domain trust at
    all. `temp/ldmud/src/simulate.c:1526-1600` `determine_uid()` calls
    the mudlib's `H_LOAD_UIDS` / `H_CLONE_UIDS` driver hook and takes
    whatever it returns: a string (`uid = euid = that`), a two-element
    `({uid, euid})` array (distinct euid, this is how an LDMud mudlib
    grants trust), or (non-strict-euids) a number. All policy is
    mudlib-side; the driver has no backbone concept to port.

**Built.**

  - **`Config`** (`include/amlp/config/Config.hpp` + `src/config/
    Config.cpp`): new `auto_trust_backbone` key, `bool
    autoTrustBackbone()` getter, default `false` (this driver's stand-in
    for the compile flag, matching the vendored `#undef`). Parsed next to
    `dialect`, accepts `1`/`true`/`yes`/`on`. Every existing config file
    that never sets it is byte-for-byte unaffected.
  - **`UidModel`** (`include/amlp/security/UidModel.hpp`): new `bool
    autoTrustBackbone = false` field. `resolveObjectUids()`'s header doc
    rewritten to list all three real branches in real's own order.
  - **`ObjectManager::captureBootUids()`** (`src/object/
    ObjectManager.cpp`): after capturing `backboneUid`, sets
    `uidModel_.autoTrustBackbone = config_.autoTrustBackbone()`. The
    `get_bb_uid` comment updated (it used to say the backbone uid is
    never consumed because the branch is `#undef`).
  - **`resolveObjectUids()`** (`src/security/UidModel.cpp`): the middle
    branch, taken only when `model.autoTrustBackbone && model.backboneUid
    && creatorName == *model.backboneUid && loaderEuid.has_value()`,
    returning `{*loaderEuid, loaderEuid}` (real `ob->uid = ob->euid =
    current_object->euid`). The stale `(void)model;` line is gone.
  - **Named divergence from real:** real assigns `current_object->euid`
    unconditionally, so a loader whose euid is 0 leaves the new object
    with a NULL uid that real's own `f_getuid()` then flags with
    `DEBUG_CHECK("UID is a null pointer\n")`. This driver requires
    `loaderEuid.has_value()` for the branch and otherwise falls through
    to the untrusted branch, which cannot produce a null uid.

**Row 3.2 item (3) re-scoped, not built.** Its old text asked for "a
real per-domain writable-path check ... that consults the master rather
than only `checkValidPath()`". Reading real FluffOS 2.9 `file.c:705-750`
`check_valid_path()` directly: that single function IS the entire
driver-level jail. It pushes `(path, call_object, call_fun)`, applies
`valid_write` / `valid_read`, treats a `0` return as deny and a string
return as a path rewrite, then runs `legal_path()` (`..` / `#` / space
check). No second per-domain layer exists in the driver; the
domain-to-prefix mapping is entirely in the mudlib master's `valid_*`
body. This driver's `checkValidPath()` already does that apply with the
object's `euid()` (slice 2) and already gates every relevant file efun.
The one real remaining gap is the `legal_path()` `..`/`#` check, which
`checkValidPath()`'s own comment already flags as deliberately skipped;
that is the concrete smallest next cut for row 3.2, not a new
master-consulting layer. The `call_other` capability-grant half still
has no FluffOS 2.9 analog (DGD-style) and stays deferred.

**2 new regression tests (827 total, up from 825),** both in
`test/test_lexer.cpp` next to the slice 1 / slice 2 uid tests, and
registered in `main()`:

  - `testAutoTrustBackboneResolveBranch`: drives the pure
    `resolveObjectUids()` directly (new `#include "amlp/security/
    UidModel.hpp"` in the test). Flag off, a backbone creator with a
    wiz-euid loader takes the untrusted branch (`uid = "backbone"`, euid
    0). Flag on, same inputs yield `uid = euid = "wiz"` (the loader
    euid). Branch order: when the creator equals both the loader uid and
    the backbone uid, the "same uid as the loader" branch still wins.
    The euid-0 decline: flag on, backbone creator, loader euid 0 -> the
    untrusted branch, not a null uid. A non-backbone creator is
    unaffected by the flag. Flag on but `backboneUid` unset (master
    defined no `get_bb_uid()`) -> nothing matches, untrusted branch.
  - `testAutoTrustBackboneEndToEndViaConfigKey`: through
    `ObjectVarHarness`, one harness built with `auto_trust_backbone: 1`
    in its config and one without (default). A master
    (`kBackboneTrustMaster`) whose `creator_file()` returns `"backbone"`
    for a `/b*` path, `"wiz"` for a `/w*` path. A `/wiz_loader` that runs
    `seteuid(getuid())` in `create()` (euid becomes `"wiz"`) then
    `clone_object("/bb_lib")` from an LPC function. Flag on: the clone's
    uid and euid are both `"wiz"`. Flag off: the clone is `"backbone"` /
    euid 0. Also asserts `uidModel().autoTrustBackbone` reflects the key
    both ways.

  The other 825 pre-existing tests re-run unchanged (no existing test
  sets the new key, and `UidModel::autoTrustBackbone` defaults false).
  Full clean rebuild from `cmake --build . --target clean`: no warnings,
  no errors. Live re-boot of the bundled `mudlib/` (`etc/driver.cfg`,
  one iteration): `master get_root_uid() = "Root"`, no failed loads, no
  undefined-function errors (the bundled mudlib never sets
  `auto_trust_backbone`, so the new branch is inert there). Every
  expected value traced by hand from `simulate.c:178-186`, not read back
  from this driver.

**Documentation.** ROADMAP rows 3.1 and 3.2 cells updated (3.1's "Still
not built" clause rewritten to separate the no-source group-membership
part from the ported traversal, plus a "Slice 3" paragraph with the full
citation trail and the two new test descriptions; 3.2's item (3)
re-scoped in place). `COMPARISON.md` not touched, per the task's
"ROADMAP and STATUS only" instruction; its 2026-09-01 sweep note's Phase
3 fraction and suite count now trail the live numbers (suite 827) and
will catch up at that file's next full sweep.**

**2026-09-01 (a further session, same day): uid / euid object trust
model, slice 2 (ROADMAP rows 3.1 / 3.2). The two items slice 1 named as
explicitly deferred: the `clone_object()` "must call `seteuid()` first"
guard, and `valid_read` / `valid_write` taking the caller's real
`euid()` as their uid argument instead of the `privs()` stand-in. 825
tests passing (up from 823). Rows 3.1 and 3.2 cells both updated; row 3.1
stays `[x] (partial)`, row 3.2 stays `[ ]` (its item (3) is still open).**

**Why this, and why now.** Slice 1 built the `uid` / `euid` data model
and the four uid efuns but deliberately stopped short of the two places
that data actually gates behavior. Both are small, both are direct
ports, and both were named on rows 3.1 and 3.2 as the next slice. No new
subsystem, no new file, no new library.

**Cited real source, read directly from disk this session.**

  - **`clone_object()` guard:**
    `temp/reference/fluffos-2.9-ds2.08/simulate.c:541-549` --
    `clone_object(const char *str1, int num_arg)` opens with
    `#ifdef PACKAGE_UIDS / if (current_object && current_object->euid ==
    0) { error("Object must call seteuid() prior to calling
    clone_object().\n"); } / #endif`, before `save_command_giver()`,
    `find_object()`, or any compile. `current_object->euid == 0` is real
    `euid == NULL` (`object.h:107-108`, `userid_t *euid`).
  - **`valid_read` / `valid_write` uid argument:**
    `temp/ldmud/src/simulate.c:3752-3851` `check_valid_path(string_t
    *path, svalue_t caller, string_t *call_fun, Bool writeflg)`. Lines
    `3778-3790` switch on `caller.type` and set `eff_user =
    caller.u.ob->eff_user`; lines `3792-3795` are `if (eff_user != NULL
    && eff_user->name != NULL) push_ref_string(inter_sp,
    eff_user->name); else push_number(inter_sp, 0);`, then
    `3797-3802` push `call_fun` and `caller` and
    `apply_master(STR_VALID_WRITE / STR_VALID_READ, 4)`. `eff_user` is
    LDMud's euid: `temp/ldmud/src/efuns.c:4985-4986` `f_geteuid()`
    returns `ob->eff_user->name`; `main.c:751,766` sets
    `master_ob->eff_user`. Real FluffOS's own `check_valid_path()`
    (`temp/reference/fluffos-2.9-ds2.08/file.c:705-750`) pushes only
    `(path, call_object, call_fun)` -- no uid argument at all, so the
    fluffos-dialect 3-arg branch does not change.
  - **The one real LDMud consumer, re-checked:** `temp/core-lib`'s
    `secure/master/security.c:153-215` defines `valid_write(string path,
    string uid, string method, object caller)` and the matching
    `valid_read`; both ignore the `uid` parameter and key entirely off
    `caller` (`isPriviledgedObject(caller)`, `userHasWriteAccess(path)`).
    `secure/master.c:20` `get_master_uid()` returns integer `1`, not a
    string, so `captureBootUids()` leaves the model inactive for
    core-lib and its `valid_*` still receive the `privs()` fallback,
    unchanged.

**Built.**

  - **`ObjectManager::cloneObject()`** (`src/object/ObjectManager.cpp`):
    a guard at the very top, ahead of `normalizeFilename()` / `compile()`,
    `if (uidModel_.active() && vm_) { auto callerOb = vm_->currentObject();
    if (callerOb && !callerOb->euid().has_value()) throw LpcRuntimeError(
    "Object must call seteuid() prior to calling clone_object().\n"); }`.
    Gated on `uidModel_.active()`, this driver's `PACKAGE_UIDS`
    stand-in, the same gate `assignObjectUid()` / `captureBootUids()`
    already use. Message text is byte-for-byte real, trailing newline
    included, since a mudlib may `catch()` and string-match it. A direct
    C++ `cloneObject()` with no `current_object` (every existing test's
    call shape) does not trip it; neither does a caller that has
    `seteuid()`d itself.
  - **`checkValidPath()`** (`src/efun/EfunTable.cpp`, the shared gate
    behind all ~20 file efuns): the LDMud branch's uid argument was
    `(caller && caller->privs()) ? Value(*caller->privs()) : Value{}`;
    it is now `caller->euid()` when
    `vm.objectManager().uidModel().active()`, and `caller->privs()` only
    while the model is inactive. `std::nullopt` still maps to the
    existing empty `Value` (this slice did not also change that to a
    literal integer `0`; real pushes `push_number(0)`, noted as a
    pre-existing, unrelated nuance and left alone to keep the
    inactive-model path byte-identical). The big comment block above the
    function and the FluffOS 3-arg branch are otherwise unchanged.

**2 new regression tests (825 total, up from 823),** both in
`test/test_lexer.cpp` next to slice 1's three:

  - `testCloneObjectRequiresCallerEuidUnderActiveUidModel`: with
    `kUidAwareMaster` loaded (model active), a `/wiz_cloner` object
    (owner `"wiz"`, euid `0`) calling `clone_object("/clone_leaf")` from
    an LPC function throws, and the message contains `"seteuid() prior
    to calling clone_object"`. After the object runs `seteuid(getuid())`
    (euid becomes `"wiz"`) the identical call returns a real object. A
    direct `harness.objects.cloneObject("/wiz_cloner")` (no
    `current_object`) is shown to load fine despite the active model. A
    second harness whose master defines no `get_root_uid()` (model
    inactive) clones from a no-euid caller with no error.
  - `testValidWriteUidArgumentIsEuidUnderActiveUidModel`: LDMud dialect,
    a master that both turns the uid model on and defines a
    `valid_write(path, uid, func, ob)` that records `uid`. A `/wiz_writer`
    clone (euid `0`, and `set_privs("DifferentPriv")` applied) calls
    `write_file()`: while its euid is still `0` the recorded uid is not a
    string; after `seteuid(getuid())` the recorded uid is exactly
    `"wiz"` (the euid), never `"DifferentPriv"` (the privs). A second,
    model-inactive harness confirms the `privs()` fallback still passes
    `"FallbackPriv"` through unchanged.

  `testValidWriteReceivesRealArgumentShapePerDialect` (slice's untouched
  sibling from row 1.16) keeps passing as-is: its LDMud case loads a
  master with no `get_root_uid()`, so it exercises the inactive-model
  `privs()` path; its in-code comment was updated to say so and to point
  at the new test for the active-model euid path. The other 822
  pre-existing tests re-run unchanged. Live re-boot of the bundled
  `mudlib/` (`etc/driver.cfg`, one iteration): `master get_root_uid() =
  "Root"`, no failed loads, no undefined-function errors. Full clean
  rebuild from `cmake --build . --target clean`: no warnings, no errors.

**Not built this slice (still on rows 3.1 / 3.2).** Row 3.1 items (c)
real uid/gid group membership and domain-trust traversal, and (d) the
per-domain filesystem jail / `call_other` capability grants (row 3.2).
Row 3.2's own item (3), a real per-domain writable-path check in front
of `write_file` / `rename` / `mkdir` / `rm` that consults the master
beyond `checkValidPath()`, is likewise still open; its items (1) and (2)
are what this slice closed.

**Documentation.** ROADMAP rows 3.1 and 3.2 cells updated (3.1 gains a
"Slice 2" paragraph with the full citation trail and the new test
descriptions; 3.2's slice list marks (1) and (2) done, pointing back at
3.1's slice 2). `COMPARISON.md` not touched, per the task's "ROADMAP and
STATUS only" instruction; its 2026-09-01 sweep note's Phase 3 fraction
and suite count now trail the live numbers (row 3.1 still one partial
row, suite 825) and will catch up at that file's next full sweep.

**2026-09-01 (a further session, same day): uid / euid object trust
model, slice 1 (ROADMAP row 3.1). The real FluffOS `uid` / `euid` data
model plus the four uid efuns (`getuid`, `geteuid`, `seteuid`,
`export_uid`), backed by real per-object state assigned at boot and per
load/clone. `src/security` is no longer an empty directory. 823 tests
passing (up from 820). ROADMAP row 3.1 -> `[x] (partial)`, row 3.2
scoped.**

**Why this, and why now.** The small efun/sprintf modifier sweep (rows
2.57 through 2.62) had reached diminishing returns. The uid/security
model (rows 3.1 + 3.2) is Phase 3's largest single open cluster and a
real gate on running third-party FluffOS mudlibs: `void create() {
seteuid(getuid()); }` is in essentially every mudlib object, and every
one of those calls threw `undefined function or efun: seteuid` before
this slice. `src/security/` had been an empty directory (`.gitkeep` +
`instruct.md`) since the repo's start.

**Scoped first (same turn).**

  - **Real FluffOS model, read directly from
    `temp/reference/fluffos-2.9-ds2.08/`:** `object.h:107-108` gives
    `object_t` three separate security fields, `userid_t *uid` (owner),
    `userid_t *euid` (effective owner), and `char *privs` (`:111`, the
    string this driver already had). `packages/uids.c` has the
    `userid_t {char *name;}` struct, `add_uid()` interning via an AVL
    tree of shared strings, and the four `f_*` bodies.
    `packages/uids_spec.c` has the signatures. `master.c:107-138`
    `set_master()`: at first load `master_ob->uid =
    set_root_uid(get_root_uid())`, `master_ob->euid = master_ob->uid`,
    then `set_backbone_uid(get_bb_uid())` (apply names `get_root_uid` /
    `get_bb_uid`, `applies_table.c:14,12`). `simulate.c:132-206`
    `give_uid_to_object()`, run from `init_object()` before `create()`:
    ask `master->creator_file(add_slash(obname))`, then if
    `creator_name == current_object->uid->name` the new object gets
    `uid = current_object->uid` and `euid = current_object->euid`
    ("same uid as the loader"), else `uid = add_uid(creator_name)` and
    `euid = NULL` (`AUTO_SETEUID` is `#undef` in the vendored
    `local_options`; so is `AUTO_TRUST_BACKBONE`, so that middle branch
    is not taken). `f_seteuid`: a nonzero int is `bad_arg`; int `0`
    sets `euid = NULL` and returns 1; a string asks
    `master->valid_seteuid(this_object(), str)` and real
    `MASTER_APPROVED` (`master.h:7`) treats an undefined apply (returns
    `-1`) as approval, only an explicit integer `0` denies.
    `f_export_uid`: errors "Illegal to export uid 0" if the caller's
    euid is NULL, returns 0 if the target already has an euid, else
    sets `target->uid = current_object->euid` and returns 1.
    `simulate.c:546-547`: `clone_object()` errors "Object must call
    seteuid() prior to calling clone_object()" if the caller's euid is
    0.
  - **LDMud (`temp/ldmud/`):** `func_spec:667-668` and
    `doc/efun/getuid`/`geteuid` confirm the same `getuid` (aliased
    `creator()` since 3.2.1@47) / `geteuid` concept, root apply renamed
    `get_master_uid`; `seteuid` folded into `configure_object()` and
    `export_uid` dropped, so those two stay FluffOS-shaped.
  - **This driver had:** one `LpcObject::privs_` string,
    `query_privs`/`set_privs`, `ObjectManager::initPrivsForObject()`
    (calls `master->privs_file()` at load), and
    `queryMasterUid()`/`MasterUidBoot` (a 31-line boot helper that
    queries `get_root_uid` and returns the string for `main.cpp` to
    print, storing nothing). No `uid`/`euid` fields, no uid efuns.
    `valid_read`/`valid_write` (`EfunTable.cpp`) pass `caller->privs()`
    as a documented stand-in for the real euid.
  - **Corpus:** ~529 `seteuid(`, ~441 `getuid(`, ~354 `geteuid(`, ~49
    `export_uid(` call-site lines across the vendored corpora plus the
    bundled `mudlib/`; six `local_options.*` variants define
    `PACKAGE_UIDS`; the bundled `mudlib/single/master.c` defines
    `get_root_uid`/`get_bb_uid`/`creator_file` and
    `inherit/master/valid.c` defines `valid_seteuid`.

**Built.**

  - New `security` library: `include/amlp/security/UidModel.hpp` +
    `src/security/UidModel.cpp`. `UidModel { optional<string> rootUid,
    backboneUid; bool active(); }` and the pure `resolveObjectUids()`
    distillation of `give_uid_to_object()` (creator vs loader compare,
    the two real outcomes). No dependency beyond the standard library.
    Wired into `CMakeLists.txt` before `src/object`, and linked into
    the `object` library.
  - `LpcObject`: `uid_` / `euid_` (`optional<string>`, `nullopt` ==
    real `euid == NULL`), with `uid()`/`setUid()`/`euid()`/`setEuid()`.
  - `ObjectManager::captureBootUids()`: run from `loadMasterObject()`
    right after `master_` is assigned. Applies `get_root_uid` (then
    LDMud's `get_master_uid` as a fallback), then `get_bb_uid`; stores
    both strings; sets `master_->uid = master_->euid = rootUid`. Leaves
    the model inactive when `get_root_uid()` is undefined, this
    driver's stand-in for real's `PACKAGE_UIDS` compile flag.
  - `ObjectManager::assignObjectUid()`: run from `loadObject()` and
    `cloneObject()` after `initPrivsForObject()` and before `create()`
    (so a `create()` body's own `seteuid(getuid())` sees the right
    owner). No-op unless `uidModel_.active()`. Asks
    `master->creator_file("/path")`, then `resolveObjectUids()`.
  - `EfunTable.cpp`: `getuid` / `geteuid` / `seteuid` / `export_uid`,
    real bodies. `geteuid` reads a function-pointer argument's owner
    via `Closure::owner`. `seteuid`'s master gate matches
    `MASTER_APPROVED`: undefined `valid_seteuid` -> approved, integer
    `0` return -> denied, void return -> denied, any other -> approved.

**Named divergence from real.** `give_uid_to_object()` real-destructs
an object whose `creator_file()` is missing or returns a non-string;
this driver instead assigns `uid = rootUid` and keeps the object, so a
partly-uid-aware mudlib still boots. Interned-string sharing (real
`add_uid()`'s AVL tree) is not reproduced.

**Explicitly deferred, named on row 3.1 and row 3.2.** The
`clone_object()` euid guard; `valid_read`/`valid_write` switching their
uid argument from `privs()` to `euid()`; real uid/gid group membership
and domain-trust traversal; the per-domain filesystem jail and
`call_other` capability grants (row 3.2, scoped this session, kept
`[ ]`).

**3 new regression tests (823 total, up from 820):**
`testUidModelBootCaptureAndPerObjectAssignment` (boot capture sets
`rootUid`/`backboneUid` and the master's own uid/euid; a load whose
creator matches the loader inherits both uid and euid; a load whose
creator differs gets `uid = creator`, `euid = 0`; `getuid`/`geteuid`
including the function-pointer form), `testSeteuidGeteuidExportUid
Semantics` (the `create() { seteuid(getuid()); }` idiom lifts euid from
0 to the owner uid; `seteuid(0)` clears it; `export_uid` grants a
no-euid target the caller's identity, returns 0 for a target that
already has an euid, and errors for a caller whose own euid is 0),
`testSeteuidDeniedByMasterAndUidEfunsWithoutTheBootModel` (a
`valid_seteuid` returning 0 denies and leaves euid unchanged; with a
master that defines no `get_root_uid` the model stays inactive, no
owner uid is assigned, but the efuns still work and a master with no
`valid_seteuid` approves every `seteuid`). Every expected value traced
by hand from `packages/uids.c` / `simulate.c`. Also live-booted the
bundled `mudlib/`: `master get_root_uid() = "Root"`, no failed loads.
The existing 820 re-run unchanged (the `mudlib_stub` master defines no
`get_root_uid`, so the model is inactive for every pre-existing test).

**Documentation.** ROADMAP row 3.1 -> `[x] (partial)` with the full
citation trail, remaining-scope list, and the named divergences; row
3.2 scoped (still `[ ]`). `COMPARISON.md` not touched this session, per
the task's "ROADMAP and STATUS only" instruction: its 2026-09-01 sweep
note's Phase 3 fraction (1/9) and rollups now trail the live count by
one row (3.1) and will catch up at the file's next full sweep.

**2026-09-01 (a further session, same day): `sprintf` `%f` float
specifier, `%i` alias of `%d`, and the `+` / space pad-prefix flags.
`sprintf` now formats a float argument with `%f` (six-place default,
explicit precision, field width, `-` / `|` / `0` justification), treats
`%i` as a plain alias of `%d`, and honors the `+` and ` ` pad-prefix
flags on `%d` / `%i` / `%f`, instead of throwing `sprintf: unsupported
format specifier '%f'`. 820 tests passing (up from 819). New
`ROADMAP.md` row 2.62.**

**Why this slice.** Continues the same one-package-at-a-time discipline
of rows 2.57 through 2.61: extend an already-registered efun to the real
argument shapes its own body rejects. `sprintf`'s own top comment
already listed `%f`, `%X`, and the `" "` / `"+"` pad-prefix flags among
the real modifiers it does not implement, and row 2.60's own STATUS
entry repeated that list. A corpus pass ranked `%f` well above the
others: it is the widest real gap of the remaining sprintf set.

**Real surface.** `temp/reference/fluffos-2.9-ds2.08/sprintf.c:911`
`case 'f': finfo |= INFO_T_FLOAT;`; `:906-909` `case 'd': case 'i':
finfo |= INFO_T_INT;` (so `i` is a plain alias of `d`); `:869-873`
`case ' ': finfo |= INFO_PP_SPACE;` / `case '+': finfo |=
INFO_PP_PLUS;`.

**Real semantics, read from `sprintf.c:1134-1211` (the shared numeric
branch) plus `add_justified()` at `:562`.** A C cheat string is built
as `"%"` + (`"+"` for `INFO_PP_PLUS`, `" "` for `INFO_PP_SPACE`) +
(`".<pres>"` only when `if (pres)` is true, i.e. precision is nonzero) +
the type char (`f` for float, `ld` for int), then run through C
`sprintf` on `carg->u.real` (float) or `carg->u.number` (int). The
field width `fs` is NOT part of the cheat string: it is applied
afterward by `add_justified()` (pad to `fs`, `-` left / `|` centre /
default right; a `0` field-width prefix pads with `'0'` ahead of any
sign, exactly as this driver's existing `%d` field-width block already
does). The float argument must be a real: `sprintf.c:1180` errors
`"Incorrect argument type to %f"` when the cheat ends in `f` and
`carg->type != T_REAL`, so an int is not silently coerced. A precision
of 0, whether from a bare `%f` or an explicit `%.0f`, means C's default
six places, per real's own `if (pres)` guard, a deliberate quirk
reproduced here.

**Corpus evidence, checked before implementing.** Grepped every
vendored corpus under `temp/` (`core-lib`, `dead-souls`, `es2_mudlib`,
`lima`, `nightmare3`, `reference-lpc-mud-library`, `wiz_tools`, `lil`)
plus `mudlib/` for `sprintf(...%...f)`: about 20 distinct files, roughly
60 call-site lines, dominated by `%.2f` (14), `%9.2f` (9), `%3.1f` (6),
`%2.2f` (5) for weights, money, CPU-time, and percentages (e.g. a
weight-to-string simul_efun's `sprintf("%.2f lbs", w)`), plus `%+4.2f`
(signed) and a scattering of `%.3f` / `%16.4f` / `%10.4f`. `%i` / `%3i`
appears in FTP-daemon and status-report code across several corpora;
`%+d` in roughly 8 call-site lines (signed stat deltas). Every one of
these previously threw `sprintf: unsupported format specifier`.

**Built** (`EfunTable.cpp`, `sprintfImpl`).

  - The modifier scan now also consumes `+` and ` ` (a lone one
    directly after `%` or another modifier; ordinary literal spaces
    between specifiers are never in that position, covered by a
    regression test).
  - `spec == 'i'` joins the `spec == 'd'` branch. `%d` / `%i` with a
    sign flag format through C `"%+lld"` / `"% lld"` instead of
    `std::to_string`; without a flag the existing `std::to_string` path
    is unchanged.
  - A new `spec == 'f'` branch requires a `double` value (throws
    `sprintf: %f argument is not a float` otherwise, matching real),
    builds the `"%[+ ][.pres]f"` cheat string, sizes the result with a
    zero-length `std::snprintf` probe, formats into the `piece` string,
    and lets the existing shared field-width / justify / zero-pad block
    finish it.
  - No new include or dependency (`std::snprintf` was already in use in
    this file).

**Scoped out, named.** `%X` (capital hex), `#` (table mode), `@`
(array-spread), `'X'` (custom pad string), and the `:` colon
precision-tie combined with `%f` are still not implemented (zero corpus
evidence; `%f` with `:` appears nowhere). `+` / ` ` on `%o` / `%x` is
accepted by the modifier scan but has no effect, matching C where those
flags are meaningless for the unsigned conversions.

**1 new regression test (820 total, up from 819):**
`testSprintfFloatSpecifierAndSignFlags` in `test/test_lexer.cpp` --
the six-place default (`"%f"` over `3.5` gives `"3.500000"`), explicit
precision (`"%.2f"` over `3.14159` gives `"3.14"`), the
`%.0f`-is-six-places quirk, field width right (`"%9.2f"`), left
(`"%-9.2f"`), and zero-pad (`"%06.2f"` gives `"003.50"`), the `+` sign
flag on both a positive (`"+3.50"`) and a negative (`"-3.50"`) value,
the ` ` space flag (`" 3.50"`), the int-argument-to-`%f` throw, `%i`
and `%3i`, `%+d` / `% d`, and a literal space between two specifiers
(`"%d %d"`) staying literal. Every expected string was traced by hand
from `sprintf.c`'s cheat-string build and `add_justified()`, not read
back from this driver. The existing 819 re-run unchanged.

**Documentation updated to match.** One new `ROADMAP.md` row, 2.62
(`[x]`, full citation trail in its own cell). `COMPARISON.md` had its
full 2026-09-01 phase-fraction re-derivation earlier this same session;
its numbers were nudged for this row too so the two documents stay
consistent rather than a row apart: Phase 2 35/63 (56%), rollups 62/99
(~63%) excluding the 5 DGD-only Phase 1 rows and 62/104 (~60%)
including them, running test total 820. `sprintf`'s own top comment in
`EfunTable.cpp` updated to move `%f` / `%i` / `+` / ` ` from the "not
implemented" list to the "implemented" list.

**2026-09-01: `implode()` function/fold form, and non-string skipping
in the join form. The already-registered `implode` efun now accepts a
function pointer as its 2nd argument (a real left fold, with or without
a seed) and, in the string-separator form, skips non-string elements
instead of throwing on them, both matching real FluffOS. 819 tests
passing (up from 818). New `ROADMAP.md` row 2.61.**

**Why this slice.** Continues the one-package-at-a-time discipline of
rows 2.57 through 2.60: extend an already-registered efun to the real
argument shapes its own body rejects, rather than registering a new
name. `implode`'s own code comment already said "the function-per-
element form real implode() also supports is not implemented," and row
2.58's own STATUS entry listed `implode(arr, function [, seed])` (the
reduce form) as a known remaining gap, "lower frequency than the two
above and needs a closure trampoline. Noted, not scheduled here." The
closure trampoline is not actually new work: `map`/`filter`/
`sort_array` already call `vm.callClosure()` from inside their efun
bodies.

**Real surface.** `temp/reference/fluffos-2.9-ds2.08/func_spec.c:76`
`mixed implode(mixed *, string | function, void | mixed);`;
`efun_defs.c:99` `{"implode",F_IMPLODE,0,0,2,3,TYPE_ANY,T_ARRAY,
T_STRING|T_FUNCTION,T_ANY,T_ANY,...}` (2 to 3 args, 2nd arg string or
function). Identical in the current clone
`temp/fluffos/src/packages/core/core.spec:55`.

**Real semantics, read line by line from
`temp/reference/fluffos-2.9-ds2.08/efuns_main.c:1032` `f_implode()`
plus `array.c:395` `implode_string()` and `array.c:428`
`implode_array()`.**

  - 2nd arg a string is the join form. Real `implode_string()` counts
    only `T_STRING` elements: the separator goes between consecutive
    string elements only, non-string elements are skipped (not
    stringified, not an error), and an array with no string element
    returns `""`. A 3rd argument alongside a string separator is a hard
    error (`"Third argument to implode() is illegal with implode(array,
    string)"`).
  - 2nd arg a function is a left fold. The function is called with
    exactly two arguments `(accumulator, element)` each step. With a
    seed (3 args): `acc = seed`, then `acc = f(acc, elem)` for every
    element; an empty array returns the seed unchanged. Without a seed
    (2 args): an empty array returns int `0`; a one-element array
    returns that element unchanged with the function never called;
    otherwise `acc = arr[0]`, then `acc = f(acc, arr[i])` for `i` in
    `1..n-1`. Real `implode_array()`'s own `if (!v) *dest = const0`
    mid-fold path (a call through a destructed-object function pointer)
    is approximated by a null-`Closure` guard returning `0`, since this
    driver's `vm.callClosure()` throws rather than returning a
    sentinel, the same choice `map`/`filter` already make.
  - `check_for_destr(arr)` (real drops destructed object refs to `0`
    before folding or joining) is not reproduced: no corpus call site
    depends on it, and the prior body did not do it either.

**Built.** The `implode` registration in `EfunTable.cpp` rewritten to
dispatch on `args[1]`'s type: `std::string` takes the join branch
(now element-skipping, with the 3rd-argument error), `std::shared_ptr<
Closure>` takes the fold branch (seed and no-seed shapes), anything
else throws, and 2 or 3 args only (a 4th throws). No new include or
dependency.

**Corpus evidence, checked before implementing.** Most of the ~1200
`implode(` call sites across the vendored corpora (`core-lib`,
`dead-souls`, `es2_mudlib`, `lima`, `nightmare3`,
`reference-lpc-mud-library`, `wiz_tools`, `lil`, `mudlib`) pass a
literal string separator, already handled; the visible function-shaped
hits are almost all a nested `implode(map(...), "\n")` where `implode`
itself still takes a string. The fold form's own conformance case is
pinned directly by `temp/lil/single/tests/efuns/implode.c`'s own 9
assertions (`implode(({1,2,3}),(: $1 + $2 :)) == 6`,
`implode(({1,2,3}),(: $1 + $2 :),"") == "123"`,
`implode(({}),(: $1 :),666) == 666`, `implode(({}),(: $1 :)) == 0`,
`implode(({555}),(: $1 + 5 :)) == 555`, plus the join-form
non-string-skipping cases `implode(({1,2,3}),"foo") == ""` and
`implode(({1,"foo","bar"}),"bazz") == "foobazzbar"`);
`temp/dead-souls/lib/doc/efun/strings/implode` documents the same. The
prior body's "throw on a non-string element" was itself a divergence
from real `implode_string()`; the new behavior is strictly more
lenient, so no existing all-string call site changes.

**1 new regression test (819 total, up from 818):**
`testImplodeFunctionFoldFormAndNonStringSkipping` in
`test/test_lexer.cpp` -- every `temp/lil` assertion above, plus the
string-separator-with-a-3rd-argument error path. Every expected value
was traced by hand from `f_implode()` / `implode_string()` /
`implode_array()`, not read back from this driver. The existing 818
re-run unchanged.

**Documentation updated to match:** one new `ROADMAP.md` row, 2.61
(`[x]`, full citation trail in its own cell).

**Same session, before this slice: two documentation-only passes, both
recorded here since this file is the dev log.** (1) `COMPARISON.md` was
given a full phase-fraction re-derivation from `ROADMAP.md`'s and
`STATUS.md`'s current state (its Phase 2 table was stale at 14/45 and
its running test total at 743). Re-counted directly from the current
checkboxes: Phase 0 16/16, Phase 1 real-blockers 10/11, Phase 2 33/61
at sweep time (34/62 after this slice), Phase 3 1/9; rollups 60/97
(~62%) excluding the 5 DGD-only Phase 1 rows and 60/102 (~59%)
including them, 61/98 and 61/103 after this slice. The brief that
requested that refresh carried its own stale figures (Phase 2 "6/22,
27%", 761 tests, rollup "33/58"); the discrepancy is flagged in
`COMPARISON.md`'s own new "Re-swept 2026-09-01" note, live checkboxes
used instead. New Phase 2 row-by-row table added there; row 3.9's own
gameplay depth and its two driver-side fixes (sprintf missing-mapping-
key, array-form `call_other`) written up. (2) `ROADMAP.md` row 1.8
(LDMud `#'lfun::`/`#'sefun::`/`#'var::` closure-literal prefixes, the
one open real Phase 1 blocker) had its own standing "re-check the
zero-evidence claim fresh" caution acted on: `grep -rnE
"#'[A-Za-z_][A-Za-z0-9_]*::"` across every vendored mudlib corpus
returns exactly one hit, the already-implemented `#'efun::` form; zero
for the three prefixes this row still covers, outside `temp/ldmud/`'s
own docs. The 2026-08-20 finding stands, the row stays deferred, Phase
1's real-blocker fraction stays 10/11, and hotboot (row 2.3) is
recorded in that cell as the recommended next target since it builds on
row 2.4's landed dual-persistence work.**

**Why this slice.** Row 3.9's own STATUS notes explicitly flagged this
as a "confirmed real need" reachable in ordinary play:
`cmds/mortal/_emote.c`'s `cmd_emote()` uses `%=` and every `emote`
command hit `sprintf: unsupported format specifier '%='`. It is also
one of the most widespread sprintf modifiers across the vendored
corpora: about 40 files and 30 distinct call-site lines
(`core-lib`, `dead-souls`, `lima`, `nightmare3`,
`reference-lpc-mud-library`, `wiz_tools`), overwhelmingly the
`sprintf("%-=" + WIDTH + "s", str)` "wrap a message to the terminal
width" idiom, plus the hanging-indent `sprintf("%s%-=*s", prefix, ...,
body)` form.

**Real semantics, traced from `temp/reference/fluffos-2.9-ds2.08/
sprintf.c` (`INFO_COLS`, `add_column()`, `add_justified()`).**

  - `%=Ns` (or `%=NO`) word-wraps its string argument to a column N
    wide. `add_column()` scans up to the wrap width or an embedded
    `\n`; if the fill would split a word and a space was seen in the
    window, it backs up to the last space; a forced `\n` is consumed,
    otherwise the run of spaces at the break is skipped. Wrapped
    segments are joined by `\n`.
  - The wrap width is the precision if one was given, clamped to the
    field size, else the field size itself (`if (pres > fs) pres = fs;
    (pres) ? pres : fs`). A field size is required (`if (!fs)
    ERROR(ERR_CST_REQUIRES_FS)`).
  - `-` left-justifies each wrapped line, `|` centres it (the odd pad
    character on the leading side, `i = fs/2 + fs%2`), neither
    right-justifies (`add_justified()`'s default). Trailing pad on a
    wrapped line is added only when `add_justified()`'s `trailing` flag
    is set, which `string_print()` computes as "more format text other
    than `\n` follows the specifier". For the common lone `%=` at the
    end of a format, `trailing` is 0, so a left-justified wrap gets no
    trailing pad at all.
  - Continuation lines are indented to the output column where `%=`
    began (`add_pad(0, start - curpos)`), so a `%s` or literal prefix
    before `%=` produces a real hanging indent.
  - `%=` on a numeric specifier (`%=5d`) is a silent no-op: real's INT
    branch never enters the `INFO_COLS` path, so it formats exactly
    like `%5d`.

**Built.** In `sprintfImpl` (`EfunTable.cpp`): `=` is now parsed in the
same modifier run as `-` / `:` / `|`, setting a `colMode` flag. Two new
file-local helpers: `countColStringSpecs(fmt)` counts `%=` specifiers
targeting `s` / `O` (computed once per call), and `wrapForColumn(text,
width)` does the greedy `add_column()`-style word wrap. When a `s` /
`O` specifier carries `colMode`, a dedicated block computes the wrap
width and the output start column, wraps the argument, and emits the
segments (first inline, each continuation preceded by `\n` plus the
start-column indent) with an `add_justified()`-equivalent per line,
then `continue`s past the normal single-line field-width path. A
numeric specifier ignores `colMode` and falls through unchanged.

**Scoped out, named.** The interleaved MULTI-column form (two or more
`%=` string specifiers in one format, the wizard file-listing table
`sprintf("%=9s %=8s %=7s ...", ...)` and `sprintf("%-=25s %-=25s
%-=25s\n", ...)` idioms, about 8 corpus lines) needs the
row-at-a-time rebuild real `string_print()` does across a linked list
of column states. `countColStringSpecs` detects it and throws a clear
`sprintf: multi-column '%=' layout is not implemented` error rather
than mislaying the columns. `#` (table mode), `@` (array-spread),
`'X'` (custom pad string), ` ` / `+` (positive-integer pad), `%f`, and
`%X` stay unimplemented as before, with the top comment updated to
match.

**Corpus call-site frequency, checked before implementing.** Grepped
every vendored corpus under `temp/` plus the bundled `mudlib/` for a
`%=` inside a `sprintf` format: about 40 files. Of the distinct
call-site lines, roughly 22 carry a single `%=` (the word-wrap idiom,
handled here) and 8 carry three `%=` (the multi-column table, scoped
out). The single-`%=` lines include the `_emote.c` path that surfaced
the gap, message-wrapping simul-efuns across several mudlibs, and
`%=Nd)` menu-numbering (numeric, the no-op path).

**1 new regression test (818 total, up from 817):**
`testSprintfColumnModeWordWrap` -- `%-=10s` over a 25-character
sentence wraps to `"the quick\nbrown fox\njumps"`; a trailing `\n` in
the format is appended as normal; default (no `-`) right-justifies each
wrapped line (`"%=10s"` over `"ab cd ef"` gives `"  ab cd ef"`); `|`
centres (`"%=|8s"` over `"hi"` gives `"   hi"`); an embedded `\n`
forces a break; a `%s` prefix hang-indents the continuation lines
(`"NOTE: aaa bbb\n      ccc ddd"`); `*` dynamic field width feeds the
wrap width; `%=5d)` formats as `"   42)"`; a short string that needs no
wrap comes back verbatim; and two `%=` string specifiers, or a `%=`
with no field width, both throw. Every expected string was traced by
hand from `add_column()` / `add_justified()`, not read back from this
driver. The existing 817 re-run unchanged.

**Documentation updated to match:** one new `ROADMAP.md` row, 2.60
(`[x]`, full citation trail in its own cell). `COMPARISON.md` not
touched this pass.

**2026-08-30 (a further session, same day): `get_dir(string, int
flags)` stat-flag argument. The already-registered `get_dir` efun
extended to accept its optional 2nd argument instead of throwing on
it: `flags == -1` now returns `({ name, size, mtime })` triples, every
other value returns the plain name list. 817 tests passing (up from
816). New `ROADMAP.md` row 2.59.**

**Why this slice.** Row 2.58's own STATUS entry named this as the
next-highest-frequency case of the same class as rows 2.57/2.58: an
already-registered efun that rejects an argument shape real accepts and
the corpora use. `get_dir` threw `LpcRuntimeError("get_dir: flags
argument not implemented")` on any non-zero 2nd argument, and about 49
distinct real call-site lines across the vendored corpora pass one (26
pass `-1`, 19 pass `0x10`, a few `0x07` / `0x17`). Self-contained,
filesystem plus array building, no new type, no VM work.

**Real surface.** `func_spec.c:124` `mixed *get_dir(string, int
default: 0);`; `efun_defs.c:94` records 2 args, the 2nd `T_NUMBER`.

**Real semantics, traced from `file.c` `get_dir()` + `encode_stat()`,
identical in the pinned 2.9 tree and the current clone
`temp/fluffos/src/packages/core/file.cc`.**

  - `encode_stat()` special-cases only `flags == -1`: it emits a
    3-element array `({ name, size, mtime })` where `size` is
    `(st->st_mode & S_IFDIR) ? -2 : st->st_size` (a directory reports
    `-2`, a regular file its byte size) and `mtime` is `st_mtime`.
  - Every other `flags` value falls to `encode_stat()`'s plain-name
    branch. So `0`, and the `0x10` some newer mudlibs pass, all behave
    identically: each entry is just the name string. A non-`-1` flag is
    accepted, not an error.
  - The result is sorted by name in both forms: real
    `qsort((void *) v->item, count, ..., (flags == -1) ? parrcmp :
    pstrcmp)`, where `parrcmp` compares `item[0].u.string`.
  - A path that `stat()`s as a single existing file (not a glob, not a
    directory) returns a 1-element array via `encode_stat()`.
  - An invalid path returns int `0`.

**Built.** The registered `get_dir` lambda in `EfunTable.cpp` now reads
the optional 2nd argument (must be an int, else a clear throw),
computes `wantStat = (flags == -1)`, and threads a small `makeEntry()`
helper through both the directory-listing and glob branches that
returns either a bare name string or, under `-1`, the
`({ name, size, mtime })` triple (an extra `::stat()` per entry for the
size and mtime). A `sortByName()` pass over the result, keyed on the
name element for either shape, was added to match real's `qsort`. The
glob-in-final-component matching, `.`/`..` exclusion, single-file case,
and `checkValidPath` gating are unchanged.

**Named local choices, neither a silent divergence.**

  1. This driver's `get_dir` previously returned entries in raw
     `readdir` order; it now sorts by name for every form, including
     the no-flag path. Real always sorts, so this is a move toward
     real, not away from it. The one pre-existing `get_dir` regression
     test (`testGetDirMatchesGlobPatternInFinalPathComponentOnly`)
     sorts the names itself before asserting, so it is unaffected.
  2. A non-existent path: this driver returns an empty array where real
     `get_dir()` returns int `0` for a missing parent directory. A
     pre-existing minor divergence in this driver's `get_dir`, not
     introduced or fixed here, left out of this row's scope.

**Corpus call-site frequency, checked before implementing.** Grepped
every vendored corpus under `temp/` (`core-lib`, `dead-souls`,
`es2_mudlib`, `lima`, `nightmare3`, `reference-lpc-mud-library`,
`wiz_tools`, `lil`) plus the bundled `mudlib/` for a 2-argument
`get_dir(`: about 49 distinct call-site lines, 26 passing `-1`
(name/size/time triples, e.g. `get_dir(dir + tmp[x-1], -1)`,
`get_dir("/", -1)`, `get_dir(sprintf("%s/", directory), -1)`), 19
passing `0x10` (e.g. `get_dir("/lib/modules/*", 0x10)`,
`get_dir(PlayerCommands, 0x10)`), plus a few `0x07` / `0x17`. Every one
previously threw against this driver.

**1 new regression test (817 total, up from 816):**
`testGetDirFlagsFormReturnsStatTriplesOrNames` -- `get_dir("/", -1)`
returns `({ name, size, mtime })` triples with a 5-byte file reporting
`5`, a 3-byte file `3`, and a subdirectory `-2`, an `mtime` that is a
plausible recent Unix timestamp, and the entries sorted by name;
`get_dir("/*.txt", -1)` is the glob plus the triple shape;
`get_dir("/alpha.txt", -1)` is a 1-element array holding one triple;
`get_dir("/", 16)` (i.e. `0x10`) returns the same plain name strings as
`get_dir("/")`; a missing path yields nothing without throwing; and a
non-int flags argument throws. Every expected size is the literal byte
length of the test file, not read back from this driver. The existing
816 re-run unchanged.

**Documentation updated to match:** one new `ROADMAP.md` row, 2.59
(`[x]`, full citation trail in its own cell). `COMPARISON.md` not
touched this pass.

**2026-08-30 (a further session, same day): buffer value type, first
slice. A new `Buffer` alternative in the `Value` variant plus the
buffer efuns that need only the type and no external dependency:
`bufferp`, `allocate_buffer`, `read_buffer`, `write_buffer`,
`to_buffer`/`_to_buffer`, and a `crc32` buffer-argument arm. 816 tests
passing (up from 815). New `ROADMAP.md` row 2.33a; row 2.33 split so it
now covers only the iconv-dependent charset efuns.**

**Why this slice.** The prior turn's own assessment named the buffer
type as the single highest-leverage next move: one `Value`-level
addition unblocks roughly 15 efuns across core/compress/sockets. A
scoping pass (also recorded that turn) confirmed a clean carve-off: the
type plus `bufferp`/`allocate_buffer`/`read_buffer`/`write_buffer`/
`to_buffer` need nothing but the new kind, while `string_encode`/
`string_decode`/`buffer_transcode`/`set_encoding`/`query_encoding` need
a charset-conversion dependency, `compress`/`uncompress` need zlib, and
binary socket mode needs the socket layer. This slice is exactly the
dependency-free set. Real corpus demand is strong: about 77 real
`.c`/`.lpc` call sites across `dead-souls`, `es2_mudlib`, `lima`,
`nightmare3`, `reference-lpc-mud-library`, and `lil` (`read_buffer`
~32, `bufferp` ~13, `allocate_buffer` ~13, `write_buffer` ~11,
`to_buffer` ~8), every one of which previously threw.

**The type.** `struct Buffer { std::vector<unsigned char> bytes; }` in
`include/amlp/vm/Value.hpp`; `std::shared_ptr<Buffer>` appended LAST in
`ValueVariant`. Appending is deliberate: `src/vm/Value.cpp`'s
`valuesEqual()` has the only `data.index()` read in the tree (an
`a.index() != b.index()` fast path), and there is no `std::visit`,
numeric `std::get<N>`, `variant_size`, or exhaustive variant switch
anywhere in `src/`, `include/`, or `test/` (grepped directly before
touching the variant), so a new trailing member is purely additive. A
buffer is fixed-size at `allocate_buffer()` time and never grows (real
`buffer.c` `write_buffer()` refuses to write past the end for exactly
that reason); it holds only bytes, never a `Value`, so it can never
take part in a reference cycle, which makes it strictly simpler than
`Array`/`Mapping` for the eventual row 3.3 GC. Lifetime is the same
`shared_ptr` model as the other heap kinds, no weak_ptr, no finaliser.

**Type-system arms** (all traced to real source):

  - `valuesEqual` (`Value.cpp`): pointer identity, matching
    `eoperators.c` `f_eq`'s `T_BUFFER` case `(sp-1)->u.buf ==
    sp->u.buf`. Two distinct `allocate_buffer()` results with identical
    bytes are NOT equal; a second variable aliasing one IS.
  - `isTruthy` (`Value.cpp`): a buffer is always truthy in real FluffOS
    (its type tag is above `T_NUMBER`), independent of byte length.
  - `valueToDebugString` / `%O` (`EfunTable.cpp`): renders `"<buffer>"`,
    matching real `sprintf.c` `svalue_to_string()` (no contents, no
    length). Without this arm a buffer would have hit the function's
    `"!ERROR: GARBAGE SVALUE!"` fallthrough.
  - `typeof` (`EfunTable.cpp`): returns `"buffer"`, real `interpret.c`
    `type_names[]`. Was falling through to `"int"`.
  - `sizeof` / `strlen` / `strwidth` (`EfunTable.cpp`): the buffer's
    byte length. Was falling through to `0`, which would break the
    corpus-common `read_buffer(b, sizeof(b))`.
  - `copy` (`EfunTable.cpp`): deep-copies to a fresh, identity-distinct
    buffer, matching real `copy()`. Was falling through to a shared
    reference.

**Serialisation: unchanged by design, and it matches real.** Real
`object.c` `save_svalue()`'s switch has no `T_BUFFER` case at all (it
falls straight through and writes nothing, confirmed by reading it), so
real FluffOS cannot save a buffer in an object variable either.
`serializeValue()` (save_object) and `serializeWorldValue()`
(dump_state) here write void (`'N'`) for a buffer, so the file stays
well formed and the slot round-trips as `0`; `writeRealSaveValue()`
(save_variable) raises a clear error. All three match the choices
already established for object references and function pointers.
Comments added at each site. 58 existing save/restore tests unaffected
(none enumerate `Value` kinds; behavior for a buffer is the same "as
void" the catch-alls already produced).

**Efuns, semantics from `temp/reference/fluffos-2.9-ds2.08/buffer.c` +
`efuns_main.c`, and the current clone for `to_buffer`:**

  - `bufferp(mixed)`: 1 for a buffer, else 0 (`f_bufferp()`).
  - `allocate_buffer(int size)`: `size < 0` errors `"Illegal buffer
    size."` (real `buffer.c` `allocate_buffer()`); size 0 is legal (a
    zero-length buffer); otherwise `size` zero bytes. A fixed 256 MB
    local ceiling stands in for real's config-defined `max_buffer_size`
    (this driver has no such config, the same situation `add_a()` /
    `replace_html()` are in for `max_string_length`). A non-int
    argument throws.
  - `read_buffer(string | buffer, void | int start, void | int len)`
    (`f_read_buffer()` + `buffer.c` `read_buffer()`): buffer form
    returns a STRING of the bytes in `[start, start+len)`, `len` 0
    meaning "to the end", negative `start` counting from the end,
    stopping at the first NUL inside the range; `len < 0`, a still-
    negative `start` after adjustment, or `start >= size` all return
    int `0`. String form reads that FILE's bytes over the same window
    and returns a NEW buffer; a missing file or empty read returns int
    `0`. Path-gated exactly like `read_bytes`.
  - `write_buffer(string | buffer dest, int start, string | buffer |
    int data)` (`f_write_buffer()` + `buffer.c` `write_buffer()`):
    buffer-dest form writes `data` into the buffer in place: an int as
    its 4 bytes in network byte order (`htonl`), a string or buffer as
    its raw bytes. A write that would run past the fixed end is refused
    and returns `0`; a successful write returns `1`; negative `start`
    counts from the end. String-dest form writes `data` (string only)
    to the file at that offset, creating it if absent, returning `1` /
    `0`.
  - `to_buffer` / `_to_buffer` (current clone `f__to_buffer()` /
    `svalue_to_buffer_bytes()`; 2.9 has no such efun): a buffer passes
    through by identity; a string becomes a buffer of its raw bytes; an
    int array becomes one byte per element, erroring `"Illegal array
    item in buffer conversion: must be ints 0..255."` on any element
    out of range or not an int; any other argument type errors.
  - `crc32` gains a buffer-argument arm (`func_spec.c`: `int
    crc32(string OR_BUFFER)`), the identical CRC over the buffer's
    bytes. The string path and its no-final-complement quirk are
    unchanged.

**Explicitly deferred, unchanged from the scoping:**

  - `string_encode` / `string_decode` / `buffer_transcode` /
    `set_encoding` / `query_encoding`: stay on row 2.33, need a
    charset-conversion dependency (iconv or equivalent).
  - `compress` / `uncompress` / `compress_file` / `uncompress_file`:
    row 2.42, need zlib. Zero corpus call sites regardless.
  - Binary socket mode (`socket_write` / `socket_read` of a buffer):
    `src/net` socket-layer work.
  - VM operators on buffers: `b[i]` (byte read), `b[i..j]` (sub-
    buffer), `b[i] = x`, range assignment, `buffer + buffer` / `buffer
    + string`. Until their own follow-up row a buffer operand throws an
    honest "cannot index" / kind-mismatch, no silent-wrong hazard.
  - `buffer` as an LPC type keyword: the lexer has no such token, so a
    declared `buffer b` does not parse; runtime buffer values work
    through `mixed`, the same as `class` values do. A small parser
    follow-up.
  - `to_int(buffer)` / `to_float(buffer)`: real reads a buffer as a
    4-byte network-order int / an 8-byte double. Not in this slice's
    scoped efun list; a follow-up.

**Built.** `include/amlp/vm/Value.hpp` (struct + variant member),
`src/vm/Value.cpp` (`isTruthy`, `valuesEqual` arms),
`src/efun/EfunTable.cpp` (`valueToDebugString`, `serializeValue`,
`writeRealSaveValue`, `typeof`, `sizeof`, `copy` arms; the six efun
registrations after `classp`; the `crc32` rework),
`src/persist/StateSerializer.cpp` (one comment at the catch-all). No
new include or dependency. Full build clean.

**1 new regression test (816 total, up from 815):**
`testBufferTypeAndCoreEfuns` -- `bufferp` true only on a buffer;
`allocate_buffer` size 0 and 5 and the negative-size throw; `sizeof` /
`strlen` / `typeof` on a buffer; a `write_buffer` then `read_buffer`
round trip including the read stopping at the first NUL, the past-the-
end write refused with `0`, a fitting write returning `1`, a negative
`start`, and the network-byte-order int case (`1094861636` writing
`'A' 'B' 'C' 'D'`); `to_buffer` from a string and from an int array,
the out-of-range and wrong-type element throws, and buffer pass-through
by identity; equality as pointer identity (two allocations unequal, an
alias equal, `copy()` distinct); and the file-path forms of
`read_buffer` / `write_buffer` against a scratch file. Expected bytes
hand-computed from `buffer.c`, not read back from this driver. The
existing 815 re-run unchanged.

**Documentation updated to match:** one new `ROADMAP.md` row, 2.33a
(`[x]`, full citation trail in its own cell); row 2.33's own cell
amended to record the split and that it now covers only the iconv
charset efuns. `COMPARISON.md` not touched this pass.

**2026-08-30 (a further session, same day): `strsrch()` / `strstr()`
correctness fix. The already-registered `strsrch` efun (and its alias
`strstr`) was rejecting an int-char needle and misreading its 3rd
argument as a start index when real treats it as a direction flag. 815
tests passing (up from 814). New `ROADMAP.md` row 2.58.**

**Why this slice.** A targeted pass, before picking the next spec-sweep
slice, for other already-registered efuns being called across the
vendored corpora with an argument count or type the current body does
not accept, i.e. the same class of bug row 2.57's `replace_string`
occurrence-range gap was. Grepped all eight corpora (`core-lib`,
`dead-souls`, `es2_mudlib`, `lima`, `nightmare3`,
`reference-lpc-mud-library`, `wiz_tools`, `lil`) plus the bundled
`mudlib/`. Findings, ranked by real call-site frequency:

  1. `strsrch` / `strstr` -- 76 distinct affected call-site lines. The
     int-char needle form (`strsrch(flags, '1')` and similar, 51 lines)
     threw; the 3rd-argument form (33 lines, almost all the literal
     `-1` backward-search idiom) silently returned a wrong index. Fixed
     this session (below).
  2. `get_dir(path, flags)` -- ~49 distinct call-site lines, all
     currently throwing `LpcRuntimeError("get_dir: flags argument not
     implemented")`. 26 pass `-1` (name/size/time triples), 19 pass
     `0x10` (directories only), a few `0x07` / `0x17`. Left as a
     follow-up row: reproducing it means a `stat()` per matched entry
     and building the real `({ name, size, mtime })` sub-array shape,
     more surface than this session's fix and a separate slice.
  3. `implode(arr, function [, seed])` -- the reduce form. Present in
     the corpora (`implode(pieces, with)` where `with` is a closure,
     `implode(split(s, pat), repl)`), lower frequency than the two
     above and needs a closure trampoline. Noted, not scheduled here.
  4. `base_name(string)`, `strsrch`-unrelated `member_array` 4th
     argument: no or negligible real corpus call sites (0 literal
     `base_name("...")`), left alone.

So the highest-frequency one, `strsrch`, is fixed here.

**The bug.** `strsrch` was registered as `int strsrch(string str,
string needle, void|int start)` with the 3rd argument implemented as a
MudOS-style start index ("first index of needle in str at or after
start") and the needle required to be a string.

  - Real `func_spec.c:125`: `int strsrch(string, string | int, int
    default: 0);` (alias `int strstr strsrch(...)` at :127).
    `efun_defs.c:251`: arg types `T_STRING, T_STRING|T_NUMBER,
    T_NUMBER`, exactly 3 args after the default is filled.
  - Real `efuns_main.c:3059` `f_strsrch()` (Luke Mewburn, 930706): the
    2nd argument may be an int, `buf[0] = (char) sp->u.number` (low 8
    bits; a resulting NUL is an empty needle). The 3rd argument is a
    direction flag: `!((sp+1)->u.number)` searches left to right
    (`strchr` for a 1-char needle, `strstr` otherwise, first match);
    any non-zero value searches right to left (`strrchr` / a
    hand-rolled reverse substring scan, last match). An empty needle or
    a needle longer than the haystack returns -1 (`if (!llen || blen <
    llen) pos = NULL`). Result is the byte offset, or -1.

  So `strsrch(path, "/", -1)` -- the standard "offset of the final
  slash" path-splitting idiom, and the single most common 3-argument
  shape in the corpora -- was hitting the old `start = -1 < 0` guard
  and returning -1 every time, and `strsrch(flags, '1')` was throwing.

**Built.** The `strsrchImpl` lambda in `EfunTable.cpp` (shared by both
`strsrch` and `strstr`) rewritten: accept 2 or 3 arguments; take the
haystack and, for a string needle, the needle up to their first
embedded NUL (real's C-string functions stop there, the same named
choice rows 2.52/2.55 made); for an int needle take `n & 0xFF` as the
single character, an empty needle if that is NUL; the optional 3rd
argument must be an int and any non-zero value selects a right-to-left
search (`std::string::rfind`), zero or absent a left-to-right one
(`find`); an empty needle or one longer than the haystack returns -1.
A non-string, non-int needle, a non-int flag, and more than 3
arguments all throw. No new include or dependency.

**Corpus call-site frequency, checked before implementing.** Grepped
all eight corpora plus `mudlib/`: 51 call-site lines use an int-char
needle (`strsrch(flags, '1')`, `strsrch(flags, 'l')`, `strsrch(flags,
'C')`, `strsrch(prop, '/', -1)`, `strsrch(msg, '\n')`, ...), 33 pass a
3rd argument (almost entirely the literal `-1` backward search:
`strsrch(path, "/", -1)`, `strsrch(remains, "@", -1)`, `strsrch(prop,
'/', -1)` four times, `strsrch(lname, "/", -1)`, ...); union 76
distinct lines, every one previously throwing or returning a wrong
index.

**1 new regression test (815 total, up from 814):**
`testStrsrchIntNeedleAndBackwardFlag` -- forward (default flag) returns
the first match, a non-zero flag the last, for both string and
int-char needles; the `strsrch(s, '/', -1)` last-slash idiom on a real
path; a NUL int needle (`0` and `256`) is an empty needle returning -1,
and only the low 8 bits count (`'A' + 256` still matches `'A'` at 0);
an empty string needle, a needle longer than the haystack, and a
genuine miss all return -1; `strstr` takes the same direction flag;
and a non-string/non-int needle (a float through a `mixed`), a non-int
flag, and a 4-argument call all throw. Every expected offset was
traced by hand from `f_strsrch()`, not read back from this driver.

**Pre-existing test corrected.** `testStrlenAndStrstrAliasesWorkByTheir
OwnNames` had `strstr("aXaXa", "a", 1)` asserting `2` under the old
start-index reading. Under the real direction-flag semantics a non-zero
flag searches right to left, so it is now `4` (the rightmost `a`), with
the test's comment rewritten to describe the flag.

**Documentation updated to match:** one new `ROADMAP.md` row, 2.58
(`[x]`, full citation trail in its own cell). `COMPARISON.md` not
touched this pass.

**2026-08-30 (a further session, same day): `replace_string()`
occurrence-range form. The already-registered `replace_string` efun
extended to accept its real optional 4th `first` / 5th `last`
occurrence-index arguments, instead of throwing on them. 814 tests
passing (up from 813). New `ROADMAP.md` row 2.57.**

**Why this slice.** Continues the same one-package-at-a-time named
sweep, but this time closes a gap in an efun this driver already ships
rather than registering a new name. `replace_string` was registered
with a 3-argument body only, and its own code comment said the
"first/last occurrence-index bounds via a 4th/5th argument" form was
"not implemented here ... nothing in this mudlib's boot/login path uses
that form". That last clause turned out to understate real demand: the
4-argument "replace only the first occurrence" shape is one of the most
common `replace_string` call shapes across the vendored corpora. Every
one of those call sites previously threw `LpcRuntimeError` against this
driver. Pure string work, no new subsystem, no dependency, no buffer
type, no LPC re-entry.

**Real surface.** `temp/reference/fluffos-2.9-ds2.08/func_spec.c:118`
declares `string replace_string(string, string, string,...);` (varargs);
`efun_defs.c:195` records min-args 3, max-args -1. The current
locally-vendored clone `temp/fluffos/src/` carries the identical
varargs signature. So this is ordinary current-and-2.9 surface, not a
2.9-only or new-since-2.9 name.

**Real semantics, traced line by line from `efuns_main.c`'s
`f_replace_string()` (:2326-2560, John Garnett's skip-table rewrite).**

  - More than 5 arguments: `error("Too many args to replace_string.")`.
  - `first` and `last` both start at 0. With `st_num_arg >= 4` the 4th
    argument is read (`CHECK_TYPES((arg+3), T_NUMBER, 4, ...)`, so a
    non-int throws); if exactly 4 arguments, `last = that value; first
    = 0` (occurrences 1..last replaced); if exactly 5 arguments, the
    4th is `first`, the 5th is `last` (`CHECK_TYPES(sp, T_NUMBER, 5,
    ...)`).
  - `if (!last) last = max_string_length;` -- `last == 0` means "no
    upper bound". This driver has no `max_string_length` at all (the
    same situation `add_a()` / `replace_html()` / `str_to_arr()` are
    in), so an unset upper bound is just unbounded.
  - `if (first > last)` -- evaluated *after* that default, so an
    unbounded upper bound never trips it -- returns the string
    unchanged.
  - `if (!plen)` (empty pattern) returns the string unchanged.
  - Every match increments `cur` whether or not it falls in range; an
    in-range match (`cur >= first && cur <= last`) is replaced, an
    out-of-range one is copied through verbatim. `if (cur == last)
    break;` stops the scan the instant the last selected occurrence is
    handled, and the remainder of the string is copied unchanged.
  - A negative bound is left as-is: it either trips the `first > last`
    early return or makes the `cur <= last` test never true. Real does
    not clamp it either.

**Named local choice, not a silent divergence.** Real's
`max_string_length` overflow path (each of several `push_svalue(
&const0u)` sites, returning a T_UNDEFINED-tagged 0) has no analogue
here, this driver having no such limit, the same kind of named drop
rows 2.55/2.56 recorded for the same reason. The 3-argument path is
byte-for-byte unchanged (it already replaced every occurrence, which is
exactly what `first = 0, last = unbounded` produces).

**Built.** The registered `replace_string` lambda in `EfunTable.cpp`
now accepts 3 to 5 arguments, parses the optional bounds with the real
4-vs-5 argument rule, and threads an occurrence counter through the
existing left-to-right `std::string::find` replacement loop. The
bad-shape error messages were reworded (the old one announced the
range form as unimplemented). No new include or dependency.

**Corpus call-site frequency, checked before implementing.** Grepped
every vendored corpus under `temp/` (`core-lib`, `dead-souls`,
`es2_mudlib`, `lima`, `nightmare3`, `reference-lpc-mud-library`,
`wiz_tools`, `lil`) plus the bundled `mudlib/` for a 4th argument to
`replace_string(`. Many real 4-argument call sites, almost all the
"replace only the first occurrence" idiom: `replace_string(line,
"Chapter", "chapter", 1)`, `replace_string(skill, "in ", "", 1)`,
`replace_string(key, "Get", "Set", 1)`, `replace_string(stamp, ".",
"/", 1)`, `replace_string(mess, "<" + chan + ">", colorchan, 1)`, and
more; plus doc pages carrying the 5-argument form's worked vectors
(`replace_string("xyxx", "x", "z", 2)` -> `"zyzx"`,
`replace_string("xyxxy", "x", "z", 2, 3)` -> `"xyzzy"`). Every one of
those previously threw against this driver. This is the first row in
the recent sweep with real, widespread corpus demand rather than
FluffOS-surface parity alone.

**1 new regression test (814 total, up from 813):**
`testReplaceStringOccurrenceRangeForm` -- the 3-argument form still
replaces every occurrence; FluffOS's own documented vectors
`replace_string("xyxx", "x", "z", 2)` -> `"zyzx"` and
`replace_string("xyxxy", "x", "z", 2, 3)` -> `"xyzzy"`; the
corpus-common `replace_string("Chapter 1, Chapter 2", "Chapter",
"chapter", 1)` -> `"chapter 1, Chapter 2"`; a `last` past the match
count and a `last` of 0 both replace all; a 5-argument `first` with
`last` 0 replaces from the Nth occurrence onward; `first > last`
returns the string unchanged; a replacement longer or shorter than the
pattern still tracks the occurrence count rather than byte offsets; an
empty pattern is a no-op even with a range; and a non-int bound (a
float through a `mixed`) and a 6-argument call both throw. Every
expected output was traced by hand from `f_replace_string()`'s own
counting rule, not read back from this driver.

**Documentation updated to match:** one new `ROADMAP.md` row, 2.57
(`[x]`, full citation trail in its own cell). `COMPARISON.md` not
touched this pass.

**2026-08-30: `func_spec.c` `USE_ICONV` conversion pair,
`str_to_arr(string)` and `arr_to_str(int *)`. A UTF-8 string decoded to
an array of Unicode code points and back, implemented as a direct codec
rather than through a live iconv dependency. Both self-contained, no LPC
re-entry. 813 tests passing (up from 812). New `ROADMAP.md` row 2.56.**

**Why this slice.** Continues the same one-package-at-a-time named sweep
that rows 2.50-2.55 followed: pick the smallest still-real gap that
needs no new subsystem, no buffer type, and no new dependency. Diffing
`temp/reference/fluffos-2.9-ds2.08/func_spec.c` against this driver's
registered efuns surfaced three unregistered names in the `#ifdef
USE_ICONV` block: `set_encoding`, `to_utf8`, `utf8_to` (all need a live
iconv translator and, for `set_encoding`, an interactive object) and the
pair `str_to_arr` / `arr_to_str`, which are pure string/array transforms.
A fourth name in that block, `strwidth`, is already registered here as
its documented non-`USE_ICONV` `sizeof` alias and was left alone. So
this slice is the two pure ones.

**2.9-only surface.** `func_spec.c:398-399` declares `int
*str_to_arr(string); string arr_to_str(int *);` inside `#ifdef
USE_ICONV`, bodied in `fliconv.c` (`f_str_to_arr()` at line 159,
`f_arr_to_str()` at line 178). The current locally-vendored clone
`temp/fluffos/src/` has neither: whole-tree grep for `str_to_arr` /
`arr_to_str` under `temp/fluffos/` hits only `docs/archive/
ChangeLog.fluffos-2.x` ("added str_to_arr, and arr_to_str efuns to
convert between strings and UTF-32 arrays") and one testsuite crasher.
FluffOS moved to always-on Unicode and dropped both. So the pinned 2.9
ds2.08 tree is the only reference, the same single-tree basis
`string_difference()` (row 2.52) noted for itself.

**Real semantics, traced from `fliconv.c`.**

  - `str_to_arr(s)`: `translate(newt->outgoing, sp->u.string,
    SVALUE_STRLEN(sp)+1, &len)` converts `s` from UTF-8 to UTF-32 over
    its length *including* the terminating NUL, then `len /= 4` and the
    resulting 32-bit units become the int array. Because the NUL is part
    of the converted input, the returned array always carries a trailing
    `0` element: `str_to_arr("AB")` is `({ 65, 66, 0 })`,
    `str_to_arr("")` is `({ 0 })`. An embedded NUL byte likewise becomes
    a `0` element in place.
  - `arr_to_str(a)`: builds `int in[size+1]`, copies each element,
    `in[size] = 0`, `translate(newt->incoming, (char *)in,
    (size+1)*4, &len)` converts UTF-32 -> UTF-8, and
    `copy_and_push_string(trans)` returns it as a C string, stopping at
    the first NUL. So `arr_to_str(({ 65, 66 }))` is `"AB"`, an embedded
    `0` truncates (`arr_to_str(({ 65, 0, 66 }))` is `"A"`),
    `arr_to_str(({}))` is `""`.
  - The two are inverses for valid text: `arr_to_str(str_to_arr(s)) ==
    s`, the trailing `0` `str_to_arr` appends being exactly the NUL
    `arr_to_str` stops on.

**Named local choices, none a silent divergence.**

  1. Implemented as a direct UTF-8 <-> code-point codec, not through a
     live iconv dependency. iconv `"UTF-8"` <-> `"UTF-32"` is exactly a
     code-point transcode; real's translator name is
     `"UTF-32//TRANSLIT//IGNORE"` on Linux, and `str_to_arr`'s one-time
     warm-up call (`translate_easy(newt->outgoing, " ")`) exists only to
     consume iconv's leading UTF-32 BOM, so the observable LPC-level
     values are BOM-free code points, which is what this codec produces.
     Same kind of named engine substitution rows 2.12/2.53 made wrapping
     PCRE2 and row 2.16's `hash()` made using OpenSSL EVP.
  2. Malformed input is ignored, matching real's `//IGNORE`: an invalid
     UTF-8 sequence (bad lead byte, truncated or bad continuation,
     overlong, surrogate, or `> U+10FFFF`) is skipped by `str_to_arr`;
     an out-of-range, surrogate, or negative code point is skipped by
     `arr_to_str`. A float array element is truncated to an integer code
     point; any other non-integer element throws.
  3. `max_string_length` is not enforced (this driver has none, the same
     situation `add_a()` / `replace_html()` are in).

  A non-string argument to `str_to_arr`, or a non-array argument to
  `arr_to_str`, throws `LpcRuntimeError`, this codebase's established
  bad-shape precedent.

**Built.** Registered in `EfunTable.cpp` immediately after `strwidth`,
sharing two file-local lambdas: `decodeUtf8ToCodePoints(src)` and
`encodeCodePointUtf8(out, cp)`. No new include or dependency.

**Corpus call-site frequency, checked before implementing.** Grepped
every vendored corpus under `temp/` (`core-lib`, `dead-souls`,
`es2_mudlib`, `lima`, `nightmare3`, `reference-lpc-mud-library`,
`wiz_tools`, `lil`) plus the bundled `mudlib/` for `str_to_arr(` /
`arr_to_str(`: zero real LPC call sites. Motivation is FluffOS-surface
parity, the same honestly-named basis as rows
2.46/2.50/2.51/2.52/2.53/2.54/2.55; UTF-8 round-tripping is
independently verifiable by hand against known code points with no
live-instance dependency.

**1 new regression test (813 total, up from 812):**
`testStrToArrAndArrToStrRoundTripUtf8` -- `str_to_arr` on ASCII, the
empty string, a 2-byte (`U+00E9`), a 3-byte (`U+20AC`) and a 4-byte
(`U+1F600`) character each decode to one code point plus the trailing
`0`; an embedded NUL byte yields a `0` element in place; a lone `0xFF`,
a truncated 2-byte lead (`0xC3`), and a surrogate sequence (`0xED 0xA0
0x80`) all drop out leaving just the terminator; `arr_to_str` performs
the inverse encodings, truncates at an embedded `0`, and skips an
out-of-range (`0x110000`) or surrogate (`0xD800`) code point;
`arr_to_str(str_to_arr(s)) == s` for three mixed ASCII/multibyte
strings; and a non-string to `str_to_arr`, a non-array to `arr_to_str`,
and a non-integer array element reached before any `0` all throw. Every
expected value is a hand-computed UTF-8 encoding of a known code point,
not read back from this driver.

**Documentation updated to match:** one new `ROADMAP.md` row, 2.56
(`[x]`, full citation trail in its own cell). `COMPARISON.md` not
touched this pass.

**2026-08-29: `dwlib.spec` markup-escaping pair, `replace_html(string)`
and `replace_mxp(string)`. Both bodied by one shared helper in real,
both self-contained pure string transforms. 812 tests passing (up from
811). New `ROADMAP.md` row 2.55.**

**Why this slice.** Same systematic sweep of `dwlib.spec` against this
driver's registered efuns that rows 2.16/2.23-2.30 and the `roll_MdN` /
`vowel` / `add_a` batch used. Of the `dwlib.spec` names still
unregistered, `replace_html` / `replace_mxp` are the only ones that need
nothing new: pure `string` in, `string` out, no LPC re-entry, no new
subsystem, no dependency, no buffer type. The rest all re-enter LPC or
walk container types: `query_multiple_short()` and `reference_allowed()`
both call back into LPC via `apply()` extensively (short-string
composition against per-object `*_short` applies; invis/creator/allowed
checks against the player and master), `replace()` delegates to
`replace_string()` over a replacement array, and `roulette_wheel()` /
`replace_objects()` / `replace_dollars()` operate on mappings, objects,
and arrays. So this slice is the two markup escapers, continuing the
same one-package-at-a-time named sweep.

**Present identically in both trees.**
`temp/reference/fluffos-2.9-ds2.08/packages/dwlib.c` and the current
clone `temp/fluffos/src/packages/dwlib/dwlib.cc` both declare `string
replace_html(string); string replace_mxp(string);` (in `dwlib_spec.c` /
`dwlib.spec`), and both body them from a single shared
`replace_mxp_html(int html, int mxp)` helper: `f_replace_html()` calls
it `(1, 0)`, `f_replace_mxp()` calls it `(0, 1)`. The 2.9 body and the
clone body are character-identical apart from the clone hoisting
`max_string_length` into a local; the escaping switch is the same.

**Real semantics, ported branch for branch from the helper's own
`switch`:**

  - `&` -> `&amp;`, `<` -> `&lt;`, `>` -> `&gt;`, unconditionally.
  - `\n` -> the MXP secure-line tag `"\e[4z<BR>"` (ESC `[` `4` `z` `<`
    `B` `R` `>`, 8 bytes) when `mxp` is set; otherwise copied through as
    a literal newline (real's `goto def`).
  - `"` -> `&quot;` when `html` is set; otherwise copied through (real's
    `case '"'` falls straight into `default` with no `break` on that
    path).
  - every other byte copied verbatim.

  So `replace_html` (html=1, mxp=0) escapes `&` `<` `>` and `"` and
  leaves newlines alone; `replace_mxp` (html=0, mxp=1) escapes `&` `<`
  `>` and rewrites each newline to `<BR>` and leaves `"` alone. This
  matches `docs/efun/contrib/replace_html.md` and
  `docs/efun/contrib/replace_mxp.md` word for word.

**Two named local choices, neither a silent divergence.**

  1. Real caps the result at the driver's `max_string_length` (its loop
     guard `dst2 - dst < max_string_length`). This driver has no
     max-string-length config at all, the same situation `add_a()` is
     in, where the real max-length error path is likewise dropped, so
     the whole input is always processed.
  2. Real reads a C string, so an embedded NUL ends the scan
     (`while(*src ...)`). This driver stops at the first NUL to match,
     rather than escaping the bytes past it, the same choice
     `string_difference()` (row 2.52) made for the same reason.

  A non-string argument or a zero-argument call throws
  `LpcRuntimeError`, this codebase's established precedent for an
  unsupported argument shape.

**Built.** Registered in `EfunTable.cpp` immediately after `add_a`, both
sharing one file-local `replaceMxpHtml(src, html, mxp)` lambda that
mirrors real's shared helper. No new include or dependency.

**Corpus call-site frequency, checked before implementing.** Grepped
every vendored corpus under `temp/` (`core-lib`, `dead-souls`,
`es2_mudlib`, `lima`, `nightmare3`, `reference-lpc-mud-library`,
`wiz_tools`, `lil`) plus the bundled `mudlib/` for `replace_html(` /
`replace_mxp(`: zero real LPC call sites. The only hits anywhere under
`temp/` are FluffOS's own docs and testsuite. Motivation is
FluffOS-surface parity, the same honestly-named basis as rows
2.16/2.24/2.46/2.50/2.51/2.52/2.53/2.54; markup escaping is
independently verifiable against hand-written strings with no
live-instance dependency, and FluffOS ships its own conformance stubs
for both (`testsuite/single/tests/efuns/replace_html.lpc`,
`replace_mxp.lpc`), one of whose vectors is reused below.

**1 new regression test (812 total, up from 811):**
`testReplaceHtmlAndReplaceMxpEscapeMarkup` -- `replace_html("<b>&</b>")`
is `"&lt;b&gt;&amp;&lt;/b&gt;"`, `replace_html("say \"hi\"")` is `"say
&quot;hi&quot;"`, the FluffOS testsuite vector (`"<b>"` no longer
present in the result of `replace_html("<b>&</b>")`), a newline left
alone, an all-safe string and the empty string returned verbatim;
`replace_mxp("<b>&</b>")` escapes the same three entities but
`replace_mxp("say \"hi\"")` leaves the quotes, `replace_mxp("a\nb")` is
`"a"` + ESC + `"[4z<BR>b"` and `replace_mxp("\n\n")` is two back-to-back
tags, `replace_mxp("plain")` is unchanged; and a wrong-type argument
(through a `mixed`) or a missing argument throws for both efuns.
Expected strings were traced by hand from the helper's switch, not read
back from this driver.

**Documentation updated to match:** one new `ROADMAP.md` row, 2.55
(`[x]`, full citation trail in its own cell). `COMPARISON.md` not
touched this pass.

**2026-08-28 (a further session, same day): `pcre.spec`,
`pcre_replace(string, string, string *, void | int)`. The remaining
self-contained `pcre.spec` name after row 2.53's read-side trio. 811
tests passing (up from 810). New `ROADMAP.md` row 2.54.**

**Why this slice.** Row 2.53 built the three `pcre.spec` names that only
read match data and named `pcre_replace()` / `pcre_replace_callback()` /
`pcre_cache()` as still deferred. Of those three, `pcre_replace()` is the
only self-contained one: `pcre_replace_callback()` re-enters LPC once per
match (needs an apply trampoline), and `pcre_cache()` reports the
contents of an internal pattern cache this driver structures differently.
`pcre_replace()` needs nothing new (PCRE2 is already linked and wrapped;
`compileRegex()` and the `pcreCompileOptions` / `pcreMatchOptions` flag
helpers already exist), so it is this slice, continuing the same
named-deferral pattern the `matrix.spec` slices (rows 2.47-2.49) used.

**Signature** from `temp/fluffos/src/packages/pcre/pcre.spec`:
`string pcre_replace(string, string, string *, void | int);`.

**Real semantics, confirmed from `pcre.cc`.** From `f_pcre_replace()` +
the file-static `pcre_get_replace()`:

  - This is NOT an ordinary regex substitution. Match the pattern
    against the subject ONCE, then rebuild the subject with each
    SELECTED capture group replaced by the correspondingly-indexed
    element of the replacements array: group `i` by `replacements[i-1]`.
  - "Selected" means the group starts at or after the end of the last
    selected group. A nested or overlapping inner group is therefore
    skipped (its start is `< prev`), and a non-participating group is
    skipped too (real reads its `ovector` slot as `-1`, also `< prev`).
  - Text between selected groups, and the prefix before the first and
    the suffix after the last, is copied through verbatim.
  - No match: return the subject unchanged (real `f_pcre_replace()`
    does `pop_2_elems()` and returns, leaving the subject on the stack).
  - `rc == 1` (the pattern has no capture groups): return the subject
    unchanged (real's own `if (run->rc == 1)` early return).
  - `(rc - 1) != replacements->size`: error "Number of captured
    substrings and replacements do not match, %d vs %d.".
  - A non-string element in `replacements`: error "Bad argument 3 to
    pcre_replace(): replacement array must contain only strings.".
  - A non-array 3rd argument: error "Bad argument 3 to pcre_replace()".
  - The optional 4th argument is the same `pcre_flags` bitmask
    (`PCRE_I` / `M` / `S` / `U` / `X` / `A`) as `pcre_match()` /
    `pcre_extract()`.

**One narrow named divergence from real.** Real `f_pcre_replace()`
initializes its running gate from `ovector[2]` (group 1's start)
directly. A pattern whose very first group is optional and did not
participate makes real derive the prefix length from `(size_t)(-1)`
(then clamp it), producing garbage for that one pathological input. This
driver treats a non-participating group-1 start as 0 for the prefix and
the gate, so that input produces a sane result instead. Every case where
group 1 actually participated is byte-identical to real.

**Built.** Registered in `EfunTable.cpp` immediately after
`pcre_match_all`. It compiles the pattern with
`compileRegex(pattern, pcreCompileOptions(flags))`, runs one
`pcre2_match` at offset 0 with `pcreMatchOptions(flags)`, then walks the
`pcre2_get_ovector_pointer` spans applying the selection rule directly on
a `std::string` built with `+=` (no pre-sized buffer, so the real size
pre-pass is unnecessary; `PCRE2_UNSET` is checked explicitly since it is
`SIZE_MAX` rather than `-1`). No new include or dependency.

**1 new regression test (811 total, up from 810):**
`testPcreReplaceSubstitutesSelectedGroups` -- `([a-z]+) ([a-z]+)` on
`"hello world"` with `({ "HI", "EARTH" })` gives `"HI EARTH"`; two
adjacent groups (`([a-z]+)([0-9]+)` on `"abc123def"`) give `"XYdef"`
(trailing text copied through); three groups with literal gaps
(`(a).(b).(c)` on `"a-b-c"`) give `"X-Y-Z"` (gap text copied verbatim); a
nested inner group (`(([0-9][0-9])[0-9][0-9])` on `"2026"`) is not
selected, giving `"A"`; no match and a group-less pattern both return the
subject unchanged; `PCRE_I` as the 4th argument makes the uppercase word
the first match, so it is the one replaced (`"X bar"`, versus `"FOO X"`
without the flag); and a replacement-count mismatch, a non-string
replacement element, and a non-array 3rd argument all throw
`LpcRuntimeError`. Expected outputs were traced by hand from the
selection rule, not read back from this driver.

**Documentation updated to match:** one new `ROADMAP.md` row, 2.54
(`[x]`, full citation trail in its own cell). `COMPARISON.md` not
touched this pass.

**2026-08-28 (a further session, same day): `pcre.spec` read-side efuns,
`pcre_version()`, `pcre_extract()`, `pcre_match_all()`. The three
`pcre.spec` names that only read match data, continuing row 2.12's own
named deferral of the six `pcre_*` efuns it did not build. 810 tests
passing (up from 807). New `ROADMAP.md` row 2.53.**

**Why this slice.** Row 2.12 built `pcre_match()` / `pcre_assoc()` from
the separately-vendored current-FluffOS tree
(`temp/fluffos/src/packages/pcre/`, a package the pinned 2.9 ds2.08
reference never had at all, whole-tree "pcre" grep of
`temp/reference/fluffos-2.9-ds2.08` still returns nothing) and
explicitly named the other six `pcre.spec` names as out of scope that
slice. This slice takes the three that only read match data:
`pcre_version()`, `pcre_extract()`, `pcre_match_all()`. It needs nothing
new (PCRE2 is already linked and already wrapped, `compileRegex()` and
the `pcreCompileOptions` / `pcreMatchOptions` flag helpers already
exist), the same continue-the-named-deferral pattern the `matrix.spec`
slices (rows 2.47-2.49) used. `pcre_replace()` / `pcre_replace_callback()`
/ `pcre_cache()` stay deferred: `pcre_replace()` has an unusual "one
replacement string per capture group, counts must match" contract plus
non-overlapping copy logic, the callback form calls back into LPC per
match, and the cache is an internal-structure introspection this driver
organizes differently.

**Signatures** from `temp/fluffos/src/packages/pcre/pcre.spec`:
`string pcre_version(void);`,
`string *pcre_extract(string, string, void | int, void | int);`,
`mixed pcre_match_all(string, string, void | int);`.

**Real semantics, confirmed from `pcre.cc`.** From `f_pcre_version()`,
`f_pcre_extract()` + `pcre_get_substrings()`, and `f_pcre_match_all()` +
the file-static `pcre_match_all()`:

  - `pcre_version()`: real pushes the linked engine's version string
    (`pcre_version()`, PCRE1). This driver links PCRE2, so it returns
    PCRE2's version via `pcre2_config(PCRE2_CONFIG_VERSION)`, a string
    like `"10.42 2022-12-11"`. A named engine substitution, the same
    kind row 2.12 already made wrapping PCRE2 in place of the 2.9 Henry
    Spencer engine and row 2.16's `hash()` made using OpenSSL EVP.

  - `pcre_extract(subject, pattern, [include_names], [pcre_flags])`:
    match `pattern` against `subject` once. No match returns an empty
    array (real `the_null_array`). On a match, return the captured
    substrings for groups 1..N where N = `rc - 1` and `rc` is
    `pcre2_match()`'s return (one more than the highest-numbered group
    that was set); group 0, the whole match, is NOT included
    (`pcre_get_substrings()` fills `ret->item[i-1]` starting from
    `i = 1`, and the `if (run->rc != 1)` guard makes a group-less
    pattern return an empty array). A group that did not participate
    yields `""` (real reads the `ovector[-1]` slots as a zero-length
    span). If `include_names` (the optional 3rd argument, any nonzero
    int) is set, a mapping of `{named-group name: that group's captured
    value}` is appended as the last array element, covering named groups
    whose number is in 1..N. The optional 4th argument is a `pcre_flags`
    bitmask, the same `PCRE_I` / `M` / `S` / `U` / `X` / `A` set as
    `pcre_match()` / `pcre_assoc()`.

  - `pcre_match_all(subject, pattern, [pcre_flags])`: return an array
    with one element per non-overlapping match, each element itself an
    array `[whole-match, group1, ..., group_{rc-1}]` (`rc` elements,
    group 0 first this time). Iterates with the standard empty-match
    idiom: after a zero-length match, retry at the same offset with
    `PCRE2_NOTEMPTY_ATSTART | PCRE2_ANCHORED`, and if that fails advance
    one UTF-8 character. The real loop guard is `offset < s_length`
    (strictly), so a trailing zero-length match at end-of-string is not
    reported and an empty subject yields an empty array; this driver
    matches that guard exactly.

**One narrow named fidelity gap.** Real `pcre_get_substrings()` omits a
named group from the `include_names` mapping when it did not participate
(`ovector` slot `< 0`). This driver's shared group vector collapses "did
not participate" and "matched empty" both to `""`, so a non-participating
named group is mapped to `""` rather than omitted. Only visible for an
optional named group like `(?<x>a)?`; a plain `(?<x>...)` that matched is
unaffected.

**Built.** A shared local lambda `pcreMatchGroups(code, subject,
byteOffset, matchOpts, groups, mStart, mEnd)` runs one `pcre2_match` and
fills `groups` with the substring for capture groups 0..(rc-1) (an unset
group gives `""`), returning the pair count or 0 on `PCRE2_ERROR_NOMATCH`
and throwing on any other PCRE2 error. `pcre_version` calls
`pcre2_config`. `pcre_extract` compiles once, calls `pcreMatchGroups`
once, returns `groups[1..]` (dropping group 0), and when `include_names`
is set walks the pattern's name table
(`pcre2_pattern_info` `NAMECOUNT` / `NAMEENTRYSIZE` / `NAMETABLE`, each
entry a 2-byte big-endian group number then a NUL-terminated name) to
build the trailing mapping. `pcre_match_all` loops `pcreMatchGroups` with
a growing offset and the empty-match retry flags, pushing one inner array
per match. All three registered in `EfunTable.cpp` immediately after
`pcre_assoc`. No new include or dependency.

**3 new regression tests (810 total, up from 807):**
`testPcreVersionReturnsAVersionString` -- the result is a non-empty
string that starts with a digit and contains a `.`.
`testPcreExtractReturnsCaptureGroups` -- `([0-9]+)-([0-9]+)` on
`"call 555-1234 now"` gives `({ "555", "1234" })`, no match and a
group-less pattern (`[0-9]+`) both give an empty array, `(a)(b)?(c)` on
`"ac"` gives `({ "a", "", "c" })` (optional group did not participate),
`PCRE_I` as the 4th argument makes `([a-c]+)` match `"__ABC__"` as
`({ "ABC" })` while the same pattern without the flag returns an empty
array, and `include_names` on
`(?<year>[0-9]{4})-(?<mon>[0-9]{2})` over `"2026-08"` appends
`([ "year": "2026", "mon": "08" ])` as the third element.
`testPcreMatchAllReturnsEveryMatch` -- `[0-9]+` over `"a1b22c333"` gives
three single-element rows `"1"` / `"22"` / `"333"`, `([a-z])([0-9])` over
`"a1 b2"` gives `[whole, g1, g2]` rows, no match gives an empty array,
and `a*` over `"baa"` gives the leading empty match then `"aa"` with no
trailing empty (the `offset < s_length` guard). Expected match arrays
were hand-written from the patterns, not read back from this driver.

**Documentation updated to match:** one new `ROADMAP.md` row, 2.53
(`[x]`, full citation trail in its own cell). `COMPARISON.md` not
touched this pass.

**2026-08-28 (a further session, same day): `contrib.spec` string efun,
`string_difference(string, string)`, the Levenshtein edit distance. One
pure self-contained function, string in and int out, no VM internals, no
new subsystem, hand-verifiable. 807 tests passing (up from 806). New
`ROADMAP.md` row 2.52.**

**Why this slice.** After the `math.spec` vector efuns (row 2.51), a
package-by-package sweep of `temp/fluffos/src/packages/*/*.spec` against
this driver's registered efuns still shows small self-contained gaps in
`contrib.spec`. `string_difference` is the cleanest of them: a pure
function with no dependency, no new subsystem, no VM-internals contact,
and it is independently verifiable by hand-computing edit distances. The
other `contrib.spec` gaps each need something more: `test_load` the
object loader and `destruct` path, `get_os_env`/`set_os_env` a runtime
config allow-list table, the class family a class value type, `event`
the call-out/apply machinery, `has_cycle`/`find_cycles`/`break_cycles` a
value-graph walk that mutates VM-managed slots, and
`program_info`/`memory_summary`/`network_stats`/`get_garbage` are the
already-scoped diagnostic family (row 2.36).

**Genuinely new since 2.9, confirmed from source.**
`temp/reference/fluffos-2.9-ds2.08` has no `string_difference` and no
`levenshtein` anywhere in that tree (grepped the whole tree, not just
the package spec). The current locally-vendored clone
`temp/fluffos/src/packages/contrib/contrib.cc` adds both `levenshtein()`
and `f_string_difference()`, and `contrib.spec` there declares
`int string_difference(string, string);`. A grep of `EfunTable.cpp`
confirmed it was not already registered. Same category as row 2.46's
`sha1()`, the `log2()`/`round()` row, and row 2.51's vector efuns: real
current-FluffOS surface the 2.9 reference never carried, not an old
always-present gap.

**Real semantics, confirmed from `contrib.cc`.** From `levenshtein()`
and `f_string_difference()`:

  - `f_string_difference()` reads its two string arguments, and if
    `strcmp(a, b) == 0` returns 0 immediately. Otherwise it calls
    `levenshtein()`, passing the shorter string first (a speed choice
    only; the distance is symmetric).
  - `levenshtein()` strips the common prefix and the common suffix
    (noting in its own comment "This doesn't change the result"), then
    runs a one-row dynamic program: `table[j]` holds the running cost,
    `skew`/`nskew` carry the diagonal, and each cell is
    `min3(table[j-1] + 1, table[j] + 1, skew)` where `skew` counts a
    substitution when `a[i] != b[j-1]`. This is the classic Levenshtein
    edit distance with insert, delete and substitute each costing 1.
  - Both arguments are handled as C strings (`strcmp`/`strlen`), so a
    literal embedded NUL byte terminates them.

**Built.** `string_difference` registered in `EfunTable.cpp` directly
after `upper_case`, among the other contrib string efuns. It rejects a
missing second argument or a non-string argument with `LpcRuntimeError`
(this codebase's established precedent for an unsupported argument
shape, e.g. `sha1()` row 2.46). Each argument is truncated at its first
NUL byte via `.c_str()` to match the real `strcmp`/`strlen` handling, a
named local choice rather than silently scoring the bytes past the NUL.
Equal strings short-circuit to 0. Otherwise a plain single running row
of costs (`O(min(|a|,|b|))` space, the shorter string chosen as the
inner axis) computes the distance; prefix/suffix stripping is omitted
because it is a pure optimization that cannot change the answer. No new
include (`<algorithm>` and the vector/string headers are already
pulled in).

**1 new regression test (807 total, up from 806):**
`testStringDifferenceIsLevenshteinDistance` -- FluffOS's own testsuite
vectors (`testsuite/single/tests/efuns/string_difference.lpc`):
`("abc","abc")` is 0, `("abc","abd")` is 1, `("kitten","sitting")` is 3,
`("","abc")` is 3. Plus more hand-checked cases: symmetry
(`("sitting","kitten")` is also 3), a pure 3-deletion run
(`("abcdef","abc")`), a pure 3-insertion run (`("abc","abcdef")`),
both-empty is 0, `("flaw","lawn")` is 2 (delete `f`, insert `n`), and
`("gumbo","gambol")` is 2 (substitute `u`->`a`, insert `l`). A
wrong-type second argument passed through a `mixed` and a one-argument
call both throw `LpcRuntimeError`. Every expected distance was
hand-computed, not read back from this driver.

**Documentation updated to match:** one new `ROADMAP.md` row, 2.52
(`[x]`, full citation trail in its own cell). `COMPARISON.md` not
touched this pass.

**2026-08-28 (a further session, same day): `math.spec` vector efuns,
`norm()`, `dotprod()`, `distance()`, `angle()`. Four pure float-math
functions from the `math` package, the same shape as the `matrix.spec`
slices and `log2()`/`round()`: no new subsystem, no dependency, no
buffer type. 806 tests passing (up from 804). New `ROADMAP.md` row
2.51.**

**Why this slice.** After `contrib.spec`'s two timezone efuns (row
2.50), a package-by-package sweep of `temp/fluffos/src/packages/*/*.spec`
against this driver's registered efuns still shows small, self-contained
gaps that need nothing new. `math.spec` is the cleanest: it declares
`norm`, `dotprod`, `distance`, `angle` (alongside the already-done
`log2`/`round`), none of them registered here yet, all pure `<cmath>`
arithmetic over float arrays with no new subsystem, no new dependency,
and independently verifiable by hand-computed values with no live
instance. Every other open Phase 2 sweep row (2.31-2.45) still needs a
new subsystem, the buffer type, `zlib`, TLS, `fork`/`exec`, async I/O,
or a stats-tracking layer.

**Genuinely new since 2.9, confirmed from source.**
`temp/reference/fluffos-2.9-ds2.08/packages/math_spec.c` ends at
`ceil()`: there is no `norm` / `dotprod` / `distance` / `angle` (or
`f_norm` / `f_dotprod` / `f_distance` / `f_angle`) anywhere in that
tree. The current locally-vendored clone
`temp/fluffos/src/packages/math/math.spec` adds
`float norm(int *|float *);`,
`float dotprod(int *|float *, int *|float *);`,
`float distance(int *|float *, int *|float *);`,
`float angle(int *|float *, int *|float *);`, and its `math.cc` header
comment reads "Added norm, dotprod, distance, angle, log2." This is the
same category as row 2.46's `sha1()`, the `log2()`/`round()` row, and
rows 2.16/2.24: real current-FluffOS surface the 2.9 reference never
carried, not an old always-present gap.

**Real semantics, confirmed from `math.cc`.** From `norm()` /
`vector_op()` / `f_norm()` / `f_dotprod()` / `f_distance()` /
`f_angle()` in `temp/fluffos/src/packages/math/math.cc`:

  - Each array element is read as int (`T_NUMBER`) or float (`T_REAL`)
    and promoted to double; any other element type is an error. Real
    `f_norm()` raises `"norm: invalid argument 1."`; `vector_op()` (used
    by the other three) raises `"<efun>: invalid arg N."` with N being 1
    for the first vector, 2 for the second, and it inspects the *second*
    operand's element before the first. `f_angle()` runs its `dotprod`
    pass first, so a non-numeric element there surfaces as
    `"angle: invalid arg N."` (the norm-failure texts
    `"angle: invalid argument 1./2."` are unreachable once every element
    is known numeric).
  - `norm(a)` = `sqrt(sum a_i^2)`. An empty array gives `sqrt(0.0)` =
    `0.0`.
  - `dotprod(a,b)` = `sum a_i*b_i`; `distance(a,b)` =
    `sqrt(sum (b_i-a_i)^2)`. Both require equal lengths, else
    `"dotprod: cannot take the dotprod of vectors of different sizes."`
    / `"distance: cannot take the distance of vectors of different
    sizes."`
  - `angle(a,b)` = `acos(dotprod(a,b) / (norm(a) * norm(b)))` in
    radians; its size mismatch reads `"angle: cannot calculate the
    angle between vectors of different sizes."`
  - Real FluffOS operates on the driver stack in place, but none of
    these four mutate their array arguments (unlike the matrix
    package, rows 2.47-2.49). This driver reads the
    `std::shared_ptr<Array>` arguments and returns a fresh float
    `Value`; there is no aliasing contract to reproduce.
  - A call with fewer than the required array arguments throws (the
    efun validates its own arity). Real FluffOS would stack-underflow
    on `(sp-1)`.

**Corpus call-site frequency.** Grepped every vendored corpus under
`temp/` (`core-lib`, `dead-souls`, `es2_mudlib`, `lima`, `nightmare3`,
`reference-lpc-mud-library`, `wiz_tools`, `lil`) plus the bundled
`mudlib/` for `norm(` / `dotprod(` / `distance(` / `angle(`: zero real
LPC call sites (the `distance`/`angle` textual hits are all unrelated
identifiers, not efun calls). Motivation is FluffOS-surface parity, the
same honestly-named basis as rows 2.16/2.24/2.25/2.46/2.47/2.48/2.49/
2.50.

**Built.** `norm`, `dotprod`, `distance`, `angle` registered in
`EfunTable.cpp` directly after `round`, at the end of the math-package
block. One local lambda `vectorArg(v, errmsg)` reads an `Array` argument
into a `std::vector<double>`, promoting int elements and throwing
`errmsg` on a non-array or a non-numeric element. A second lambda
`vecPair(args, efun, sizemsg)` reads two vectors, applies the per-vector
`"<efun>: invalid arg N."` messages, and throws `sizemsg` on a length
mismatch. `norm` sums squares and `sqrt`s; `dotprod` sums products;
`distance` sums squared differences and `sqrt`s; `angle` accumulates
dot, `|a|^2`, `|b|^2` in one pass and returns `acos(dot / (sqrt(na) *
sqrt(nb)))`. No new include (`<cmath>` already pulled in by the math
package).

**2 new regression tests (806 total, up from 804):**
`testVectorNormDotprodDistanceKnownValues` -- `norm(({3.0,4.0}))` and
`norm(({3,4}))` are both `5.0`, `norm(({}))` is `0.0`,
`dotprod(({1.0,2.0,3.0}),({4.0,5.0,6.0}))` is `32.0`,
`dotprod(({1,0}),({0,1}))` is `0.0`, and `distance` from origin to
`(3,4)` is `5.0` for both float and int inputs. Every value is
hand-computed (all reduce to the 3-4-5 right triangle), not read back
from this driver. `testVectorAngleAndBadArgsThrow` -- `angle` of
perpendicular unit vectors is `pi/2`, of parallel vectors (same
direction, different magnitude) is exactly `0.0`, of antiparallel
vectors is `pi` (closed forms, compared with a 1e-9 tolerance); and a
size mismatch for `dotprod`/`distance`/`angle`, a non-numeric element
passed through a `mixed`, a non-array argument, and a one-argument
`dotprod` call all throw `LpcRuntimeError`.

**Documentation updated to match:** one new `ROADMAP.md` row, 2.51
(`[x]`, full citation trail in its own cell). `COMPARISON.md` not
touched this pass.

**2026-08-28 (a further session, same day): `contrib.spec` timezone
efuns, `zonetime(string, int)` and `is_daylight_savings_time(string,
int)`. Two pure, self-contained functions from a real 2.9 package,
picked as the smallest still-real gap now that `matrix.spec` is closed.
804 tests passing (up from 802). New `ROADMAP.md` row 2.50.**

**Why this slice.** After `matrix.spec` (rows 2.47-2.49), the remaining
open Phase 2 sweep rows (2.31-2.45) each need something substantial:
row 2.32 a telnet-protocol-extension family, 2.33 the buffer value type,
2.34 a runtime-mutable config table, 2.35 per-frame `defer`/`finally`
plumbing in the VM, 2.36 a driver-internals diagnostic family, 2.38 a
UID/EUID trust hierarchy, 2.39 per-socket TLS, 2.41 a domain/author
stats subsystem, 2.42 `zlib` plus the buffer type, 2.43 `fork`/`exec`
process spawning, 2.44 real async I/O through `Scheduler`, 2.45 an FFI
family plus `libffi`. `zonetime` / `is_daylight_savings_time` need none
of that: they are two small, deterministic `<ctime>` conversions, no new
subsystem, no new dependency (`<cstdlib>` added for `setenv`), and both
are independently verifiable against hand-computed system-libc output
with no live instance.

**Which names, and the real source cited.** Signatures are identical in
both trees: `temp/reference/fluffos-2.9-ds2.08/packages/contrib_spec.c`
and the current clone `temp/fluffos/src/packages/contrib/contrib.spec`
both declare `string zonetime(string, int);` and `int
is_daylight_savings_time(string, int);`. Grep of `EfunTable.cpp`
confirmed neither was registered. The rest of `contrib.spec` is either
already implemented or already scoped as a deferred family (rows 2.36
and similar).

**Real semantics, confirmed from source.** From `packages/contrib.c`'s
own `f_zonetime()` / `f_is_daylight_savings_time()` plus
`set_timezone()` / `reset_timezone()` (John Viega's 1996 timezone
efuns, header comment "efuns for doing time zone conversions. Much
friendlier than doing all the lookup tables in LPC"), re-checked
against the identical current-clone `contrib.cc`:

  - Both work by pointing libc at the named zone: set the `TZ`
    environment variable, call `tzset()`, do the conversion, then
    restore the previous `TZ` and `tzset()` again. Real `set_timezone()`
    does this with `putenv()` and a static buffer; this driver uses
    `setenv()` / `unsetenv()` for the same effect without aliasing a
    static buffer into the environment.
  - `zonetime(tz, clock)`: `ctime` of `clock` computed in zone `tz`,
    with the trailing newline stripped (real: `retv[len-1] = '\0'`).
    `ctime`'s fixed `"Www Mmm dd hh:mm:ss yyyy"` form, space-padded day,
    so a single-digit day has two spaces before it:
    `zonetime("UTC", 1000000000)` is `"Sun Sep  9 01:46:40 2001"`. The
    2.9 source uses `ctime()`; the current clone uses `ctime_r()` and
    raises `"bad argument to zonetime."` when it returns null. Both
    matched (this driver uses `ctime_r` and the same error text).
  - `is_daylight_savings_time(tz, clock)`: `localtime` of `clock` in
    zone `tz`, returns `(tm_isdst > 0)` as 0 or 1. The current clone
    clamps a negative `clock` to 0 and returns -1 on `localtime_r`
    failure; both matched.
  - TZ is a process-global. This driver runs efuns synchronously on one
    thread (the same assumption real FluffOS makes), so the set/restore
    pair is not racing anything.

**Corpus call-site frequency.** Zero real LPC call sites in any vendored
corpus under `temp/`. These two were already surveyed in the
six-corpus efun ranking as 0-weight tail entries (`src/efun/instruct.md`
names them explicitly in its "look only for anything a fresh read might
have missed" note); re-confirmed this pass. The only `zonetime` /
`is_daylight_savings_time` hits anywhere under `temp/` are FluffOS's own
testsuite. Motivation is FluffOS-surface parity, the same honestly-named
basis as rows 2.16/2.24/2.25/2.46 and the matrix slices.

**Built.** `zonetime` and `is_daylight_savings_time` registered in
`EfunTable.cpp` directly after `localtime`, sharing one local lambda
`withTimezone(tz, body)` that saves the current `TZ`, sets the requested
zone, runs `body`, then restores `TZ` exactly (re-`setenv` if there was
a previous value, `unsetenv` if there was not) and `tzset()`s again.
`zonetime` calls `ctime_r` inside `body` and strips a trailing newline;
`is_daylight_savings_time` calls `localtime_r` and reads `tm_isdst`.
Both reject a non-string `tz` or a missing `clock` with
`LpcRuntimeError`. `#include <cstdlib>` added for `setenv`/`unsetenv`.

**2 new regression tests (804 total, up from 802):**
`testZonetimeFormatsClockInNamedZone` -- `zonetime("UTC", 1000000000)`
is exactly `"Sun Sep  9 01:46:40 2001"`, `zonetime("UTC", 0)` is
`"Thu Jan  1 00:00:00 1970"`, `zonetime("EST5", 1000000000)` is
`"Sat Sep  8 20:46:40 2001"` (fixed UTC-5, no DST rule), a one-argument
call and a non-string `tz` (through a `mixed`) both throw, and a later
call in a different zone is unaffected by an earlier one, proving `TZ`
is restored. `testIsDaylightSavingsTimeReflectsZoneAndDate` --
`"UTC"` is 0 at any date; `"EST5EDT,M3.2.0,M11.1.0"` is 1 for a
September 2001 clock and 0 for a January 2002 clock (same zone, opposite
answers by date); a negative clock is clamped to epoch rather than
erroring; a non-string `tz` or a one-argument call throws. Expected
strings and flags were cross-checked against the system libc
(`perl -e 'localtime'`) before writing the tests, not read back from
this driver. The `"UTC"` / `"EST5"` / POSIX-rule zone strings are
parsed by glibc directly, so the tests do not need the zoneinfo
database installed.

**Documentation updated to match:** one new `ROADMAP.md` row, 2.50
(`[x]`, full citation trail in its own cell). `COMPARISON.md` not
touched this pass.

**2026-08-28: `matrix.spec` final slice, `lookat_rotate()` and
`lookat_rotate2()`. This completes `matrix.spec` -- all 8 declared names
are now registered (row 2.47 landed 3, row 2.48 landed 3, this slice the
last 2). 802 tests passing (up from 800). New `ROADMAP.md` row 2.49.**

**Compiler prerequisite, checked directly before scoping.** The task
was explicit that `lookat_rotate2` must not be scoped on an assumption
about argument-count support. The 2.9 `matrix_spec.c` declares
`lookat_rotate2(float *, float, float, float, float, float, float)` (7
args) inside `#if 0`, with the comment: "for this efun to work again,
the compiler needs support for min_arg > 4 ... type checking was changed
to be done for all arguments, and a limit of 4 args was imposed." That
4-argument limit lived in FluffOS's own `.spec`-generated `efun_defs.c`
argument type-checker. It has no analog in this driver, confirmed by
reading the actual code paths, not by inference:

  - `src/compiler/Parser.cpp` `parseArgList()` is an unbounded loop with
    no argument-count cap; `parseParamList()` likewise has no cap and no
    default-argument (`type name = expr`) syntax at all, so there is no
    "min_arg" / "max_arg" distinction in a signature here to be limited.
  - `src/compiler/CodeGen.cpp` `emitCallExpr()` emits `CallEfun` with
    `argCount = call.args.size()`, uncapped. There is deliberately no
    compile-time efun signature table at all: its own comment explains
    the compiler library is not linked against the efun table to avoid a
    link cycle (`efun` -> `object` -> `compiler`). So efun calls are
    never type-checked or arity-checked at compile time.
  - `src/vm/VM.cpp` `CallEfun` pops `argc` values (bounded only by the
    stack) into a `std::vector<Value>` and hands the whole thing to the
    efun lambda; `EfunTable::call()` forwards it unchanged. Each efun
    validates its own arguments.
  - Existing proof point: `pcre_assoc()` is already a registered
    5-argument efun that reads `args[4]`.

Conclusion: the compiler already supports more than 4 arguments, so per
the task's own branch ("If the compiler already supports it, scope and
build both together") both `lookat_rotate` and `lookat_rotate2` ship in
this one slice. The 7-argument `lookat_rotate2` call is exercised end to
end by the new tests (`la2_origin`/`la2_eye` are genuine 7-arg efun
calls compiled and dispatched).

**Which names, and the real source cited.** Signatures from
`temp/reference/fluffos-2.9-ds2.08/packages/matrix_spec.c`:
`lookat_rotate` declared live, `lookat_rotate2` inside `#if 0`. Grep of
`EfunTable.cpp` confirmed neither was registered (slices 1 and 2 added
the other six).

**Real semantics, confirmed from source.** From
`temp/reference/fluffos-2.9-ds2.08/packages/matrix.c`'s own
`lookat_rotate()` / `lookat_rotate2()` core functions plus the
file-static `Vector` helpers, re-checked against
`temp/fluffos/src/packages/matrix/matrix.cc` (identical math). Note both
core functions compile unconditionally in 2.9; only the
`f_lookat_rotate2()` stack-glue wrapper was ever `#if 0`, so the math
for `lookat_rotate2` is fully defined and not guesswork.

  - `points_to_array(v, pa, pb)` sets `v = pa - pb` componentwise.
  - `cross_product(v, va, vb)` sets `v = va x vb`.
  - `normalize_array(v)` divides `v` by `|v|`, but only when `|v| != 0`
    (the real `if (m)` guard). A zero-length vector is returned
    untouched, so the degenerate "look direction parallel to up" case
    produces a finite matrix, not NaNs. This driver ports the guard
    verbatim (`if (mag != 0.0)`).
  - Both efuns compute `N = normalize(lookPoint - eyePoint)`,
    `V = normalize(N x up)`, `U = normalize(V x N)`, then write the
    result rows:
      `[ U.x V.x N.x 0 ]`
      `[ U.y V.y N.y 0 ]`
      `[ U.z V.z N.z 0 ]`
      `[ U.ep V.ep N.ep 1 ]`
    where `U.ep` is the dot product of `U` with the eye point, etc.
    `matrix.c` carries two `#if 0` alternatives for the last row (the
    raw eye point, and the negated dot product); the live code is the
    positive dot product used here.
  - `lookat_rotate(m, x, y, z)`: look point is `(x, y, z)`, eye point is
    the input matrix's translation row `(m[12], m[13], m[14])`, and
    `up` is input column 0 `(m[0], m[4], m[8])`. All three are read out
    of the input matrix before it is overwritten.
  - `lookat_rotate2(m, ex, ey, ez, lx, ly, lz)`: eye point `(ex, ey,
    ez)`, look point `(lx, ly, lz)`, `up` fixed at `(0, 1, 0)`. The
    input matrix contents are not read at all, only overwritten.
  - The passed array is mutated IN PLACE and that same array is the
    return value, matching `translate()`/`scale()`/`rotate_x()` (real
    `f_lookat_rotate` does `sp -= 3`, leaving the matrix array on the
    stack as the return). This driver returns the identical
    `std::shared_ptr<Array>` it was handed.

**Local conventions carried over from slices 1-2, not new.** The
`matrixArg16` guard (the 2.9 tree read 16 slots unconditionally; real
`f_lookat_rotate` `bad_arg`'s only on args 3 and 4, reading the matrix
and `x` unchecked) rejects a short or non-float matrix argument with
`LpcRuntimeError`. Coordinates are coerced with the same `asFloat()`
helper the rest of this driver's math package uses, matching its
established int-to-float leniency rather than reproducing a union
misread.

**Corpus call-site frequency.** Already checked when row 2.47 landed:
every vendored corpus under `temp/` grepped for `lookat_rotate` (and the
other seven matrix names): zero real LPC call sites. Motivation is
FluffOS-surface parity, the same honestly-named basis as rows
2.16/2.24/2.25/2.46/2.47/2.48; viewing matrices are independently
verifiable against hand-computed values with no live instance needed.

**Built.** `lookat_rotate` and `lookat_rotate2` registered in
`EfunTable.cpp` directly after `rotate_z`, via one shared local lambda
`lookatRotate(args, name, variant2)` that validates the matrix with
`matrixArg16`, reads the eye/look/up vectors per variant, runs the
`normalize` / `cross` sequence with the zero-magnitude guard, writes the
16 result values back into the same `Array`, and returns it. Zero new
dependency (`<cmath>` `std::sqrt`, already in use by the math package).

**2 new regression tests (802 total, up from 800):**
`testLookatRotateProducesKnownViewingMatrices` -- `lookat_rotate(id,
0,0,1)` collapses to the exact identity; `lookat_rotate(id, 0,1,0)`
matches the hand-computed `[1,0,0,0, 0,0,1,0, 0,-1,0,0, 0,0,0,1]`; the
degenerate `lookat_rotate(id, 1,0,0)` (look direction parallel to `up`)
produces the expected NaN-free `[0,0,1,0, 0,0,0,0, 0,0,0,0, 0,0,0,1]`; a
`translate(id, 2,0,0)` input carries eye point `(2,0,0)` into the last
row as dot products (`m[12] == 2`); and `lookat_rotate2` (a genuine
7-argument call) overwrites a `scale(id, 5,5,5)` input with the pure
viewing matrix `[0,1,0,0, 1,0,0,0, 0,0,-1,0, 0,0,0,1]` for a zero eye
point, and the same with `m[13] == 1` for eye point `(1,0,0)`. Every
expected matrix hand-computed from `matrix.c`'s definitions, not read
back from this driver. `testLookatRotateAliasesArrayRejectsBadMatricesAndTakesSevenArgs`
-- both efuns mutate the passed array in place (reading `m[9]` / `m[10]`
after the call sees the new value), the return value aliases the passed
array, a 3-element or 15-float-plus-one-int `mixed *` matrix argument
throws `LpcRuntimeError` for both efuns, and `lookat_rotate2` called
with only 4 arguments throws (the efun enforces its own arity of 7).

**Documentation updated to match:** one new `ROADMAP.md` row, 2.49
(`[x]`, full citation trail in its own cell). Rows 2.47 and 2.48 left
as-is. `COMPARISON.md` not touched this pass (same as slices 1 and 2).

**2026-08-27 (a further session, same day): `matrix.spec` slice 2, the
`rotate_x`/`rotate_y`/`rotate_z` trio row 2.47 named as deferred. All
three are pure row-major rotation math on the same 16-float array form,
sharing the `mult_matrix` product and the `matrixArg16` validation
guard slice 1 already added. 800 tests passing (up from 798). New
`ROADMAP.md` row 2.48.**

**Why this slice.** Of the three remaining deferred subsystems (rows
2.43-2.45), `matrix.spec` is still the one with the smallest
independently-shippable next slice: no dependency, no buffer type, no
scheduler wiring, no security surface, and independently verifiable
against hand-computed rotation matrices with zero live-instance
dependency. `external_start` (2.43) still needs `fork()`/`exec()`/pipe
plumbing plus a command registry and is a real command-execution
security surface; `async_*` (2.44) still needs real background I/O
through `Scheduler`. Within `matrix.spec`, slice 1 (row 2.47) landed
`id_matrix()`/`translate()`/`scale()`; the `rotate_x/y/z()` trio is the
natural next unit (adds only the degree-to-radian constant plus trig,
already-available `<cmath>`), leaving `lookat_rotate()`/
`lookat_rotate2()` as the final matrix slice.

**Which names, and the real source cited.** Signatures from
`packages/matrix_spec.c`, byte-identical in the vendored 2.9 ds2.08
reference and the current clone: `float *rotate_x(float *, float);`
plus the `y`/`z` twins. Grep of `EfunTable.cpp` confirmed none were
registered (slice 1 added `id_matrix`/`translate`/`scale` only).

**Real semantics, confirmed from source.** From
`temp/reference/fluffos-2.9-ds2.08/packages/matrix.c`'s own
`f_rotate_x()`/`f_rotate_y()`/`f_rotate_z()` plus `rotate_x_matrix()`/
`rotate_y_matrix()`/`rotate_z_matrix()`, re-checked against
`temp/fluffos/src/packages/matrix/matrix.cc` (identical math):

  - The angle argument is in DEGREES. It is converted to radians by
    multiplying by `RADIANS_PER_DEGREE`. That constant's literal in
    `matrix.h` (both trees) is `0.01745329252`, a truncation of pi/180,
    not `M_PI/180.0`. This driver uses the same literal verbatim so its
    output matches the real package byte-for-byte, not merely to 1e-9.
  - `rotate_a(m, deg)` computes `m = m * R`, where `R` is the standard
    row-major rotation about axis `a`, with `c = cos(rad)`,
    `s = sin(rad)`:
      Rx = [1,0,0,0, 0,c,s,0, 0,-s,c,0, 0,0,0,1]
      Ry = [c,0,-s,0, 0,1,0,0, s,0,c,0, 0,0,0,1]
      Rz = [c,s,0,0, -s,c,0,0, 0,0,1,0, 0,0,0,1]
    `mult_matrix` is the same plain row-major product
    (`m[4r+c] = sum_k a[4r+k] * b[4k+c]`) slice 1 added.
  - The passed array is mutated IN PLACE and that same array is the
    return value, not a copy (`docs/efun/general/rotate_x.md`:
    "modified IN PLACE ... that same array is left on the stack as the
    return value"). This driver returns the identical
    `std::shared_ptr<Array>` it was handed.

**Local conventions carried over from slice 1, not new.** The current
clone's `"matrix transform requires a 16-element array."` /
`"...float array."` guard (absent from the 2.9 tree, which read 16
slots unconditionally) is reused here via the existing `matrixArg16`
helper: a short or non-float matrix argument throws `LpcRuntimeError`.
Real `f_rotate_x()` reads the angle via `(sp--)->u.real` with no type
check; this driver coerces via the same `asFloat()` its math package
uses, matching this codebase's own int-to-float leniency.

**Corpus call-site frequency.** Already checked when row 2.47 landed:
every vendored corpus under `temp/` (`core-lib`, `dead-souls`,
`es2_mudlib`, `lima`, `nightmare3`, `reference-lpc-mud-library`, this
project's own bundled `mudlib/`, `wiz_tools`, `lil`) grepped for
`rotate_x`, `rotate_y`, `rotate_z` alongside `id_matrix`/`translate`/
`scale`: zero real LPC call sites. Motivation is FluffOS-surface
parity, the same honestly-named basis as rows 2.16/2.24/2.25/2.46/2.47;
rotation matrices are independently verifiable against hand-computed
values (0 and 90 degrees) with no live instance needed.

**Built.** `rotate_x`/`rotate_y`/`rotate_z` registered in
`EfunTable.cpp` directly after `scale`, via one shared local lambda
`applyRotation(args, name, axis)` that validates the matrix with
`matrixArg16`, coerces the angle with `asFloat()`, multiplies by
`RADIANS_PER_DEGREE`, builds the axis rotation matrix as an identity
with four entries overwritten (matching `rotate_a_matrix`), calls
`multMatrix`, writes the result back into the same `Array`, and returns
it. Zero new dependency (`<cmath>` already in use by the math package).

**2 new regression tests (800 total, up from 798):**
`testRotateXYZProduceKnownRotationMatrices` -- rotation by 0 degrees is
exactly the identity for every axis (`cos(0) == 1.0`, `sin(0) == 0.0`
exactly), so `id * R == id`; `rotate_x/y/z(id, 90)` match the
hand-computed `Rx`/`Ry`/`Rz` at 90 degrees (checked to 1e-6, since the
truncated `RADIANS_PER_DEGREE` makes `cos(90 deg)` a small nonzero
value rather than exactly 0); two `rotate_z(m, 90)` calls compose in
place to a 180 rotation. `testRotateConvertsDegreesAndRejectsBadMatrices`
-- `rotate_z(id, 180)` gives element 0 near `-1` (degrees; if the angle
were radians `cos(180 rad)` is near `-0.598`), the return value aliases
the passed array (reading `r[6]` after `rotate_x` sees the mutation),
and a 3-element or 15-float-plus-one-int `mixed *` argument throws
`LpcRuntimeError` (checked at runtime via a `mixed *` local, not a
compile-time literal-type rejection).

**Documentation updated to match:** one new `ROADMAP.md` row, 2.48
(`[x]`, full citation trail in its own cell). Row 2.47's text left
as-is. `COMPARISON.md` not touched this pass.

**2026-08-27 (a further session, same day): with the `.spec` sweep arc
finished, the first real slice of one of the three remaining deferred
subsystems (rows 2.43-2.45) landed. Picked `matrix.spec` (row 2.45's
bucket) as the one with the smallest independently-shippable slice, and
implemented three of its eight names: `id_matrix()`, `translate(float
*, float, float, float)`, `scale(float *, float, float, float)`. 798
tests passing (up from 796). New `ROADMAP.md` row 2.47.**

**Why `matrix`, and why this slice.** Rows 2.43-2.45 are the three real
remaining gaps, each a multi-session subsystem. `external_start` (2.43)
needs real `fork()`/`exec()`/pipe plumbing plus an enumerated-command
registry this driver has no equivalent of, and is a genuine arbitrary-
command-execution security surface. `async_*` (2.44) needs real
background I/O (a thread pool or `io_uring`) wired through `Scheduler`
or any "async" efun is a blocking call wearing a callback, not a real
first slice. `matrix.spec` (2.45) is pure, deterministic float math on
16-element arrays: no new dependency, no buffer type, no scheduler
wiring, no security surface, and independently verifiable against hand-
computed matrices with zero live-instance dependency. Within it, the
smallest coherent unit is `id_matrix()` (the neutral starting matrix)
plus `translate()` and `scale()` (the two simplest transforms, sharing
exactly one helper, the row-major 4x4 multiply). The `rotate_x/y/z()`
trio (adds `RADIANS_PER_DEGREE` plus trig) and `lookat_rotate()`/
`lookat_rotate2()` (add the `Vector` helpers `normalize_array`/
`cross_product`/`points_to_array`; `lookat_rotate2` also needs the
`min_arg > 4` compiler support the 2.9 spec's own comment says was
missing, still `#if 0` there) are the natural later slices, named in
row 2.47.

**Which names, and the real source cited.** This is an old, always-
present gap, not new-since-2.9: `temp/reference/fluffos-2.9-ds2.08/
packages/` already carries `matrix.c`, `matrix.h`, `matrix_spec.c`, and
the locally-vendored current clone (`temp/fluffos/src/packages/
matrix/`) has the identical math. Signatures from `matrix_spec.c` (both
trees, byte-identical): `float *id_matrix(); float *translate(float *,
float, float, float); float *scale(float *, float, float, float);`.
Grep of `EfunTable.cpp` confirmed none of the three were registered.

**Real semantics, confirmed from source.** From
`temp/reference/fluffos-2.9-ds2.08/packages/matrix.c`'s own
`f_id_matrix()`/`f_translate()`/`f_scale()` plus `translate_matrix()`/
`scale_matrix()`/`mult_matrix()` (re-checked against the current clone,
identical): `id_matrix()` returns a fresh 16-element float array holding
the row-major 4x4 identity (`{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}`).
`translate(m,x,y,z)` computes `m = m * T`, where `T` is the identity
with elements 12,13,14 set to x,y,z. `scale(m,x,y,z)` computes `m = m *
S`, where `S` is `diag(x,y,z,1)`. `mult_matrix` is the plain row-major
product `m[4r+c] = sum_k a[4r+k] * b[4k+c]`. The passed array is
mutated IN PLACE and that same array is the return value, not a copy
(current clone's own docs, `temp/fluffos/docs/efun/general/
{id_matrix,translate,scale}.md`, state this explicitly: "the return
value is the very array you passed in, not a copy"). This driver
returns the identical `std::shared_ptr<Array>` it was handed, so LPC
aliasing matches real FluffOS exactly.

**Two deliberate local conventions, named rather than silent.** (1) The
current clone added an explicit `error("matrix transform requires a
16-element array.\n")` / `"...float array.\n"` guard that the 2.9 tree
lacked entirely (2.9 read 16 slots off `(sp-3)->u.arr` unconditionally,
over-reading a short array). This driver ports that guard: it has
tagged Values, so a short or non-float matrix argument throws
`LpcRuntimeError` rather than being misread. (2) Real `f_translate()`/
`f_scale()` `bad_arg()` only on a non-`T_REAL` 3rd or 4th argument (the
2nd, x, is read via `(sp-2)->u.real` with no type check in 2.9). This
driver coerces all three coordinates via the same `asFloat()` helper
its math package already uses, matching this codebase's own established
int-to-float leniency (see the math-package comment block right above
the matrix registration) rather than reproducing a union-misread
quirk.

**Corpus call-site frequency, checked before implementing.** Grepped
every vendored corpus under `temp/` (`core-lib`, `dead-souls`,
`es2_mudlib`, `lima`, `nightmare3`, `reference-lpc-mud-library`, this
project's own bundled `mudlib/`, `wiz_tools`, `lil`) for `id_matrix`,
`translate(`, `scale(`, `rotate_x`, `rotate_y`, `rotate_z`,
`lookat_rotate`: zero real LPC call sites. The only `translate(`/
`scale(` hits at all are unrelated (NPC language-translation code in
`core-lib`/`dead-souls`, the word "scale" in prose). So this pass's
motivation is FluffOS-surface parity specifically, named honestly, the
same basis as rows 2.16/2.24/2.25/2.46, and matrix math clears the
independent-verifiability bar those rows were held to: every expected
value in the tests is hand-computed from `matrix.c`'s definitions, no
live instance needed.

**Built.** `id_matrix`/`translate`/`scale` registered in
`EfunTable.cpp` directly after the math package's `round()`, with two
shared local lambdas: `matrixArg16` (the validate-and-fetch guard
above) and `multMatrix` (the row-major 4x4 product). `id_matrix()`
builds the 16-element identity array; `translate()`/`scale()` read the
current matrix into a `double[16]`, build the transform, call
`multMatrix`, write the result back into the same `Array`, and return
it. Zero new dependency (no `<cmath>` even -- pure arithmetic for this
slice).

**2 new regression tests (798 total, up from 796):**
`testIdMatrixAndTranslateScaleProduceKnownMatrices` -- `id_matrix()` is
the exact identity; `translate(id, 10, 0, 5)` is the identity with
elements 12,13,14 = 10,0,5; `scale(id, 2, 3, 4)` is `diag(2,3,4,1)`;
`translate(id, 1, 2, 3)` then `scale(m, 2, 2, 2)` composes to `T * S`
(translation row scaled to 2,4,6; diagonal entries to 2). Every
expected matrix is hand-computed from `matrix.c`, not read back from
this driver. `testTranslateMutatesItsMatrixInPlaceAndRejectsBadMatrices`
-- reading an element after the call sees the mutation (in place); the
return value aliases the passed array (mutating the returned ref
mutates the original, element 12 = 1*1 + 1*5 = 6 after two chained
translates); a 3-element or 15-float-plus-one-int `mixed *` argument
throws `LpcRuntimeError` (checked at runtime via a `mixed *` local, not
a compile-time literal-type rejection).

**Documentation updated to match:** one new `ROADMAP.md` row, 2.47
(`[x]`, full citation trail in its own cell). Row 2.45's own text left
as-is; 2.47 states plainly that it carves its three names out of that
bucket, so the accounting lives in the new row. `COMPARISON.md` not
touched this pass.

**2026-08-27 (a further session, same day): the systematic package-by-
package `src/packages/*/*.spec` sweep continued to `sha1.spec`, one of
the last real package spec files never checked against this driver's own
registered efuns. Its single real efun, `string sha1(string|buffer);`,
built this session as `sha1(string)` (no buffer type in this driver).
796 tests passing (up from 794), live-verified against the real running
driver via a real `eval` call. New `ROADMAP.md` row 2.46.**

**Which package, and which name was missing.** The multi-session `.spec`
sweep arc had, across prior sessions, covered `math`/`core`/`trim`/
`contrib`/`ops`/`sockets`/`pcre`/`db`/`dwlib`/`uids`/`mudlib_stats`/
`compress`/`external`/`async`/`develop`/`ffi`/`matrix`/`jsbridge` (rows
2.25-2.45) and `parser` (row 0.13a). Cross-checking the full 21-file
`temp/fluffos/src/packages/*/*.spec` list against that coverage left
exactly three never formally swept: `crypto.spec`, `sha1.spec`,
`parser.spec`. Two resolved with no new work. `crypto.spec` declares
only `string hash(string, string);` -- already done, row 2.16.
`parser.spec` declares the same 8 `parse_*` names row 0.13a already
implemented in full (signatures re-checked line by line against the
vendored file this session, `parse_my_rules`'s own `mixed
parse_my_rules(object, string, void | int);` included -- all match).
`sha1.spec` is the one with a real gap: `string sha1(string|buffer);`,
one efun, not registered anywhere in `EfunTable.cpp` (confirmed by grep,
not assumed).

**Confirmed genuinely new-since-2.9, not an old gap deferred for another
reason.** `temp/reference/fluffos-2.9-ds2.08/` has no `crypto` or `sha1`
package at all -- its `packages/` directory lists async/compress/contrib/
db/develop/dwlib/external/math/matrix/mudlib_stats/parser/sockets/uids
and nothing else, and a full-tree grep for `sha1`/`f_sha1`/`F_SHA1`
returns zero hits. This is real current-FluffOS surface the 2.9
reference never had, the same category as `hash()` (row 2.16),
`time_ns()` (2.23), `secure_random()` (2.24), `log2()`/`round()` (2.25).

**Corpus call-site frequency, checked before implementing.** Grepped
every vendored corpus under `temp/` (`core-lib`, `dead-souls`,
`es2_mudlib`, `lima`, `nightmare3`, `reference-lpc-mud-library`, this
project's own bundled `mudlib/`, `wiz_tools`, `lil`) for `sha1`: zero
LPC call sites anywhere. The only hits at all are two unrelated
JavaScript files in `dead-souls`'s web directory (`lib/www/lpmuds/
sha1.js`, `script.js`), not LPC. So this pass's motivation is
current-FluffOS-surface parity specifically -- named honestly, the same
basis rows 2.16/2.23/2.24/2.25 used, not dressed up as corpus-driven
demand -- and `sha1()` clears the independent-verifiability bar those
rows were held to: canonical SHA-1 test vectors, no live-instance
dependency.

**Real semantics, confirmed from source.** Real `f_sha1()`
(`temp/fluffos/src/packages/sha1/sha1.cc`, the locally-vendored current-
FluffOS clone) hand-rolls the SHA-1 block function inline (public-domain
code by Niyaz PK, per its own header) and `sprintf`s the five 32-bit
state words as `"%08x%08x%08x%08x%08x"` -- a plain lowercase-hex SHA-1
digest. It also accepts a `T_BUFFER` argument; this driver has no buffer
value type (rows 2.33/2.42), so only the string form is built, and a
non-string argument throws rather than being silently mishandled
(matching this codebase's own precedent: `explode_reversible()`'s empty
delimiter, an unsupported `sscanf`/`sprintf` format, `member_array()`'s
unsupported 4th argument). Real doc (`temp/fluffos/docs/efun/strings/
sha1.md`): worked example `sha1("something") =
"1af17e73721dbe0c40011b82ed4bb1a7dbe3ce29"`, plus the note "The
`hash(algo, str)` external function can handle SHA-1 and more" --
`sha1()` is the convenience spelling of `hash("sha1", str)`.

**Built.** `sha1` registered in `EfunTable.cpp` directly after `hash()`,
computing the digest via OpenSSL's EVP interface (`EVP_sha1()` plus the
same `EVP_DigestInit_ex`/`EVP_DigestUpdate`/`EVP_DigestFinal_ex` shape
and the same already-linked `-lcrypto` dependency `hash()` uses), rather
than porting `sha1.cc`'s hand-rolled block function verbatim. One local
mechanism choice, named rather than silent: SHA-1's output is fixed by
FIPS 180, so the hand-rolled and EVP paths are byte-for-byte identical
for every input by construction -- unlike `secure_random()` (row 2.24),
where the real entropy-source mechanism was itself observable and had to
be ported verbatim, there is nothing observable to diverge on here. Zero
new dependency.

**2 new regression tests (796 total, up from 794):**
`testSha1ComputesKnownDigestsIncludingTheRealDocWorkedExample` -- the
real doc's own worked example (`sha1("something")`) plus standard
vectors for `"abc"` and `""`, all cross-checked against the system
`sha1sum` before writing the test, not derived from this driver's own
output. `testSha1AgreesWithHashSha1AndThrowsOnNonStringArgument` --
`sha1(str) == hash("sha1", str)` on a third input (the classic "quick
brown fox" vector), plus a non-string argument (passed through a `mixed`
parameter so the check is a runtime one, not a compile-time literal-type
rejection) throwing `LpcRuntimeError`.

**Live-verified against the real running driver** (`./build/amlp
etc/driver.cfg`, a real TCP session, the same real bundled mudlib and
gatehouse login flow prior sessions used), via a real `eval` call:
`eval return sha1("something");` returned
`"1af17e73721dbe0c40011b82ed4bb1a7dbe3ce29"` exactly, matching the real
doc's own worked example. Driver's own log showed no errors. Test-
account/character files created during verification
(`sha1check27`/`Sha1CheckChar`) deleted afterward, matching this
project's own established cleanup precedent.

**Documentation updated to match:** one new `ROADMAP.md` row, 2.46
(`[x]`, full citation trail in its own cell). `COMPARISON.md` not
touched this pass (its Phase 2 done-count and feature table are a
separate accounting surface; this entry and row 2.46 are the record for
this package).

**2026-08-27 (a further session, same day): the LDMud-vs-FluffOS `db_*`
naming collision the prior session's `.spec` sweep found (row 2.40) is
resolved. Both real sources read directly this session, not assumed
from either side's own summary; real corpus evidence checked fresh,
confirming zero demand for a second, FluffOS-shaped target. Resolution:
dialect-gate the existing, correct, evidence-backed LDMud-shaped `db_*`
family to `dialect: ldmud` only, converting a real, previously-silent
wrong-shape bug under this driver's own default dialect into an honest
gap. 794 tests passing (up from 792), both dialect paths live-verified
against the real running driver.**

**Real signature differences, confirmed from both sides directly.**
Real LDMud (`temp/ldmud/src/pkg-mysql.c`, re-read line by line this
session, not trusted from `DbRegistry.hpp`'s own prior summary alone):
`int db_connect(string database, void|string user, void|string
password)` -- no host argument at all; `int db_exec(int handle, string
statement)` -- the real doc comment states plainly, "The result is the
handle if all went okay. If there was an error in the statement, 0 is
returned," confirmed in the body: success pushes the handle back
unchanged, a bad statement pushes plain `0`, never a string; `mixed
db_fetch(int handle)` -- single arg, "Retrieve _ONE_ line of result of
the latest SQL-action... If not more results are on the server, 0 is
returned" (own doc comment), confirmed sequential via a real
`mysql_fetch_row()` walk, one call at a time; `int db_close(int
handle)` -- "Return the handle-number on success" (own doc comment),
confirmed in the body; every one of the seven real efuns gated by
`check_privilege(instrs[F_DB_X].name, MY_TRUE, sp)`.

Real current FluffOS: this session found a real, locally-vendored
current-FluffOS clone already present in this repo's own `temp/fluffos/`
directory (a real `git log` dated August 2026, confirmed live against
`master` before trusting it as current rather than assumed stale) --
used directly rather than re-fetching from GitHub for this pass.
`int db_connect(string host, string database, string|void user,
int|void type)` -- host FIRST, then the database name, an optional
user, an optional numeric backend-type selector (`USE_MSQL`/
`USE_MYSQL`/`USE_SQLITE3`/`USE_POSTGRES`), gated by a differently-named
`valid_database("connect", info)` master apply, not `check_privilege()`
at all; `mixed db_exec(int handle, string sql)` -- the real doc comment
states plainly, "Returns number of rows in result set on success, an
error string on failure," confirmed in the body: success pushes the
raw row count, failure pushes the error *string*, never the handle;
`mixed *db_fetch(int db_handle, int row)` -- TWO arguments, the real
doc's own worked example confirms explicit 1-indexed random access
(`for(i=1; i<=rows; i++) { res = db_fetch(dbconn, i); }`), not a
sequential walk; `int db_close(int handle)` -- returns a plain `1`/`0`
success flag (whatever the backend's own real `close()` callback
returns), not the handle number. Every one of these eight real
distinctions (argument count/order on `db_connect`/`db_fetch`,
return-value meaning on `db_exec`/`db_close`, the master-apply name) is
a genuine, confirmed incompatibility with LDMud's own shape, not a
cosmetic difference -- both sides now cited from direct reads of both
real sources, not from either side's own prior summary alone.

**Corpus evidence, checked fresh across every vendored corpus, not
assumed carried forward.** Grepped `dead-souls`/`es2_mudlib`/`lima`/
`nightmare3`/`reference-lpc-mud-library`/this project's own bundled
`mudlib/` for `db_connect`/`db_exec`/`db_fetch`/`db_close(`: zero real
call sites anywhere in any of them. `core-lib` is the only real corpus
evidence this project has ever had for this row, and its own real call
sites (`secure/simulated-efuns/database.c`) do not merely happen to fit
LDMud's own shape, they actively *depend* on it: `efun::db_connect(
database, user, password)` (3 args, no host -- would silently pass the
real database name as "host" under real FluffOS's own arg order);
`while (db_fetch(dbHandle));` (single-arg sequential walk -- a real
arity mismatch, not just a different result, under real FluffOS's own
required 2-arg form); and the single most concrete finding this session
made: `dbHandle = efun::db_exec(dbHandle, sqlQuery);` -- this real line
only makes sense under LDMud's own "db_exec returns the handle on
success" contract (a same-value reassignment on success); under real
FluffOS's own "returns rows-affected" contract, this exact real line
would silently overwrite `dbHandle` with a row count instead,
corrupting every subsequent `db_exec`/`db_fetch`/`db_close` call on
that connection for the rest of that code path. **Zero real corpus
evidence anywhere supports the FluffOS shape; 100% of this project's
real corpus evidence for `db_*` requires and actively depends on the
LDMud shape specifically** -- a real demand question already settled
by the evidence, not a "current FluffOS also defines something with
this name" completeness question, confirmed rather than assumed.

**The resolution, and the reasoning behind it, stated before choosing
(the same discipline every other real design decision in this project
has used).** Two live options were on the table: dialect-gate `db_*` so
`dialect: ldmud` and `dialect: fluffos` each get their own real
signature under the same name (the precedent `shadow()`/connect-
disconnect/`replace_program()`'s own no-arg form already established
for confirmed, evidence-backed dialect divergences), or keep this
driver's own single, already-correct LDMud-shaped implementation as the
only one. Building a real, second, FluffOS-shaped target was rejected:
it would mean writing and shipping code with zero real corpus evidence
anywhere to verify it against, exactly the "purely a completeness
question, not real demand" case this project's own evidence-first
discipline exists to decline -- the same standard the entire `.spec`
sweep arc has applied throughout (`pcre_*`'s own six deferred names,
GMCP/MSDP, FFI, and so on). But the *previous* state -- `db_*`
registered unconditionally under every dialect, including this driver's
own default (`dialect: fluffos`), confirmed directly from
`EfunTable.cpp`'s own registration code, not assumed -- was a real,
separate bug independent of whether a second target is ever built: a
FluffOS-dialect mudlib author writing genuine current-FluffOS-shaped
`db_connect(host, database)` got a silently wrong result (LDMud's own
arg-order reinterpretation), not an error, the exact "looks real,
isn't" risk this project has declined to introduce elsewhere (the same
reasoning row 2.38's own `uids.spec` deferral used). **Resolution:
dialect-gate the existing, single, evidence-backed LDMud-shaped `db_*`
family to `dialect: ldmud` only** -- all seven real efuns
(`db_connect`/`db_exec`/`db_fetch`/`db_close`/`db_error`/`db_handles`/
`db_conv_string`) now check `vm.config().dialect() == "ldmud"` first and
throw a clear, explicit "not implemented under dialect '<dialect>'"
error otherwise, converting a silent wrong-shape footgun into an
honest, correctly-scoped gap -- not removing any real capability, since
every pre-existing `db_*` test already ran under an explicit
`dialect: ldmud` harness (confirmed by re-reading each one before
making this change), meaning the original row 2.15 author's own
test-level intent already assumed this exact scope even though the
driver-level code itself never enforced it until now.

**`db_commit(int)`/`db_rollback(int)` excluded outright, not merely
deferred, on a finding stronger than the doc alone.** Read directly in
`temp/fluffos/src/packages/db/db.cc`: both real C++ functions exist and
are genuinely wired (`db->type->commit`/`db->type->rollback`), but that
same file's own header comment states plainly, "No database type has
been added that supports commit or rollback, so these functions have
not been fully implemented" -- confirmed live in the actual backend
table: every real backend (mSQL/MySQL/SQLite3/Postgres) leaves that
callback null, so both efuns are unconditional no-ops across the entire
real driver today, not just per the doc's own "Not yet implemented!"
claim taken at face value. Building either here would mean inventing
behavior the real driver itself does not have. `db_status(void)` is
real, implemented, and no-argument (a whole-package status report
across every open connection, confirmed in `f_db_status()`'s own real
body), but stays out of scope for the same reason the rest of the
FluffOS-shaped family does: zero real corpus evidence, and it would be
the one `db_*` name built under the dialect this driver's own `db_*`
family does not otherwise serve.

**Built.** A shared `requireLdmudDbDialect(vm, efunName)` lambda
(`EfunTable.cpp`), checked first in all seven `db_*` registrations,
throwing a clear, explicit error naming both the efun and the current
dialect when `vm.config().dialect() != "ldmud"`.

**2 new regression tests (794 total, up from 792):**
`db_connect()` under the default `dialect: fluffos` throws the new
dialect-gate error rather than silently misinterpreting a real
current-FluffOS-shaped call (checked directly against the exact real
current-FluffOS argument order, `db_connect(host, database)`); all
seven `db_*` efuns throw the same gate under both `dialect: fluffos`
and `dialect: dgd`. The full pre-existing `db_*` cluster (row 2.15's
own 6 tests, all already written against an explicit `dialect: ldmud`
harness) re-ran unchanged, confirming the gate does not affect real
LDMud-dialect behavior at all.

**Live-verified against the real running driver, both dialect paths.**
Under the default config (`./build/amlp etc/driver.cfg`, `dialect:
fluffos`), a real TCP session, `eval return db_connect("somehost",
"somedb");` threw the exact new dialect-gate message (confirmed in the
driver's own log: `"db_connect(): not implemented under dialect
'fluffos' -- ..."`), and the connection stayed open and usable for the
next `eval` in the same session, confirming the earlier connection-
isolation fix still holds. Under a temporary scratch config (a copy of
`etc/driver.cfg` with `dialect: ldmud` added and a spare port, reverted
via deletion immediately after), a full real login flow worked
normally, and a full real `db_connect`/`db_exec`/`db_fetch`/`db_close`
round trip against a real scratch SQLite file worked exactly as row
2.15 originally verified: handles returned on `db_connect`/`db_exec`/
`db_close` (real LDMud contract, confirmed live, not just in the unit
tests), the inserted row read back correctly via `db_fetch`. Driver's
own log showed no unexpected errors on either path. Test-account/
character files and the scratch SQLite file created during verification
deleted afterward.

**Documentation updated to match:** `ROADMAP.md` row 2.15 appended with
a correction to its own original "not dialect-gated... since there is
no real FluffOS-named db_connect this driver also implements to
disambiguate against" reasoning (that reasoning conflated "not building
a second implementation" with "therefore no gating is needed" -- two
separate questions); row 2.40 rewritten from an open scoping note into
the full resolution and citation trail, marked `[x]`. `COMPARISON.md`'s
Phase 2 done-count (13/45 to 14/45) and its feature-table `db_*` row
updated to match, with a new dated re-sweep note rather than a rewrite.

**2026-08-27 (a further session, same day): the full real
`src/packages/` tree enumerated live (21 real package `.spec` files,
not guessed), cross-checked against everything this project had already
swept -- found `dwlib`/`uids`/`mudlib_stats`/`compress`/`external`/
`async`/`develop`/`ffi`/`matrix`/`jsbridge` had never actually been
checked. `dwlib.spec` yielded three small, independently-verifiable
names, built this session; the rest named and scoped as eight new
deferred rows with concrete reasons, including one real, worth-naming
finding: this driver's own already-shipped `db_*` family was built
against real LDMud's own db package, not real current FluffOS's own
`db.spec` at all. 792 tests passing (up from 789), all three built
efuns live-verified against the real running driver.**

**The enumeration, and what it found.** `curl`'d the GitHub API's own
recursive tree listing for `fluffos/fluffos@master` directly (not
assumed from memory of prior sessions' own cached file lists), filtered
to `src/packages/*/*.spec`: 21 real files, an exact, confirmed count,
matching what this project had already accumulated piecemeal across
several sessions but never formally enumerated in one pass. Cross-
referenced against what had already been swept: `math.spec`/`core.spec`/
`trim.spec`/`contrib.spec`/`ops.spec` in the immediately prior session;
`sockets.spec`/`pcre.spec`/`db.spec` when those Phase 0/2 rows were
originally built (rows 0.x, 2.12, 2.15) -- confirmed this by actually
re-running this session's own extraction-and-cross-reference method
against all three rather than trusting the "already covered" framing
outright, since the point of a systematic sweep is not re-trusting
partial coverage from a different discipline (see below for what that
re-check actually found). Ten real files had genuinely never been
checked this way at all: `dwlib`/`uids`/`mudlib_stats`/`compress`/
`external`/`async`/`develop`/`ffi`/`matrix`/`jsbridge`.

**Re-checking `sockets.spec`/`pcre.spec`/`db.spec`, rather than trusting
"already covered" at face value, surfaced two real, worth-naming
findings.** `sockets.spec`: 12 of 14 real names already implemented,
the 2 gaps (`socket_get_option`/`socket_set_option`) both genuinely
new-since-2.9 and both exclusively about TLS socket configuration this
driver has no equivalent of at all -- a real gap, correctly explained by
row 2.13 (TLS) not yet landing, not a sweep failure. `pcre.spec`: the
real gap here (`pcre_version`/`pcre_match_all`/`pcre_extract`/
`pcre_replace`/`pcre_replace_callback`/`pcre_cache`, 6 of 8 real names)
turned out to already be fully documented in `EfunTable.cpp`'s own
existing comment on `pcre_match()`/`pcre_assoc()`'s own registration --
this row's own original 2026-08-21 session already found and named all
six, deliberately excluded on zero-real-corpus-evidence grounds (this
project's own long-standing discipline for Phase 0/1 rows). Not a newly
discovered gap, confirmed and reported as such rather than re-presented
as new. `db.spec` is where the real, worth-naming finding actually is:
this driver's own already-shipped `db_connect`/`db_exec`/`db_fetch`/
`db_close` family (row 2.15) was built and cited entirely against real
**LDMud's** own `pkg-mysql.c`, never against real FluffOS's own
`db.c`/`db.spec` at all -- confirmed directly from `DbRegistry.hpp`'s
own header comment, which already states this plainly and cites the
real signature differences (`db_connect`'s own host argument, `db_exec`'s
own return shape, `db_fetch`'s own row-vs-sequential indexing, the
`db_error`/`db_handles`/`db_conv_string` names FluffOS's own package
never had at all). A real, deliberate, well-reasoned choice at the
time (LDMud was this driver's own real corpus evidence for the row, not
FluffOS), but it means this driver's own `db_*` family, despite sharing
efun *names* with real current FluffOS, does not actually honor real
FluffOS's own `db_*` *contract* -- a genuine divergence from "current
FluffOS," the exact thing this whole multi-session arc has been
checking for, found here specifically because this session re-verified
rather than trusted a prior session's own "already covered" framing.

**Two real `db.spec` names, `db_commit()`/`db_rollback()`, excluded
outright rather than deferred -- their own real docs settle it.**
Fetched live (`docs/efun/db/db_commit.md`/`db_rollback.md`): both state,
verbatim, **"Not yet implemented!"** Real current FluffOS itself has
never actually implemented either one. Building them here would mean
inventing behavior the real driver does not have, the opposite of this
project's own "port the real behavior" discipline -- confirmed via the
real doc directly, not assumed from the name looking plausible.

**Ranked by the same independent-verifiability standard as every
session in this arc.** `dwlib.spec` (11 real names, `src/packages/dwlib/
dwlib.spec`, fetched live) was found the same way `math.spec`/`trim.spec`
were: cross-referenced against `EfunTable.cpp`'s own `registerEfun()`
calls, then checked against the vendored 2.9 reference. Three cleared
the bar this session:

- **`vowel(int c)`**: a plain ASCII a/e/i/o/u check, both cases. As
  airtight as any check in this whole arc -- a fixed, tiny input space,
  fully enumerable in one test.
- **`add_a(string str)`**: "a"/"an" prefixing by phonetic sound, not
  just first-letter, with two real special cases (`"us..."` and
  `"hour..."`). Real `f_add_a()` (`dwlib.cc`, fetched live) was read
  and traced by hand against every one of the real doc's own worked/
  implied examples before writing a single line of driver code or a
  single test assertion -- fully deterministic, zero live-instance
  dependency, the exact same "port the real algorithm, verify every
  branch by hand first" discipline this project has used since row
  0.13a's own `parse_*` work.
- **`roll_MdN(int rolls, int sides, int bonus default:0)`**: real
  `f_roll_MdN()` (`src/packages/contrib/contrib.cc`, fetched live --
  declared in `dwlib.spec` but actually implemented in the contrib
  package, confirmed directly rather than assumed from the file split)
  sums `rolls` draws of `1 + random_number(sides)` plus `bonus`, but
  only when both `rolls` and `sides` are positive -- a real edge case
  (bonus not added on non-positive dice either) confirmed from the
  real guard's own scope, not assumed symmetric. Reuses this driver's
  own already-ported `random()` efun directly, the same pattern this
  driver's own pre-existing `roll_weapon_damage_dice()` combat helper
  already established -- real `roll_MdN()` has no floor-at-1 the way
  that AMLP-invented neighbor does, a real, deliberate difference
  named explicitly rather than silently copied over.

**Built.** All three registered in `EfunTable.cpp`, `roll_MdN()`
placed right next to the pre-existing `roll_weapon_damage_dice()`
helper it structurally resembles (and explicitly differs from),
`vowel()`/`add_a()` nearby.

**3 new regression tests (792 total, up from 789):** `roll_MdN()`
stays within formula-derived bounds across 3000 draws per case
(mirroring `testRollWeaponDamageDiceStaysWithinFormulaDerivedBounds
AcrossManyDraws`'s own established shape), including the real
non-positive-dice-means-no-bonus edge case; `vowel()` against every
ASCII vowel both cases plus several non-vowels; `add_a()` against
every real doc example and both special-case branches.

**Live-verified against the real running driver** (`./build/amlp
etc/driver.cfg`, a real TCP session, the same real bundled mudlib and
gatehouse login flow prior sessions used), via real `eval` calls:
`vowel('a')`/`vowel('b')` returned `1`/`0`; `add_a("apple")`/
`add_a("cat")`/`add_a("user")`/`add_a("usher")`/`add_a("hour")`
returned `"an apple"`/`"a cat"`/`"a user"`/`"an usher"`/`"an hour"`
exactly; `roll_MdN(3, 6, 0)` returned a real in-range roll (`10`);
`roll_MdN(0, 6, 5)` returned `0`, confirming the real non-positive-dice
guard live, not just in the unit test. Zero errors in the driver's own
log throughout. Test-account/character files created during
verification (`specsweep27`/`SpecSweepChar`) deleted afterward.

**Eight real names/families found and deliberately deferred, each with
a concrete reason, not dropped silently** (full citation trail in each
row's own `ROADMAP.md` cell, summarized here): `seteuid`/`geteuid`/
`getuid`/`export_uid` (`uids.spec`) -- a real, meaningful UID/EUID trust
hierarchy, directly the same gap `COMPARISON.md`'s own accounting
already names, deliberately not approximated as a bare, ungated flag
pair the way the wizard flag was, since an ungated stand-in for a
security-relevant mechanism would look real while providing none of
the real guarantees; `socket_get_option`/`socket_set_option` -- real,
but entirely TLS-only, deferred alongside row 2.13 specifically;
`db_status()` -- entangled with the real LDMud-vs-FluffOS `db_*`
contract question found above, not a quick addition; `domain_stats`/
`author_stats` (`mudlib_stats.spec`) -- needs a genuinely new,
continuously-updated per-domain/per-author bookkeeping subsystem this
driver has none of; `compress`/`uncompress`/`compress_file`/
`uncompress_file` (`compress.spec`) -- blocked on the same missing
buffer type row 2.33 already named, plus a new zlib dependency;
`external_start` (`external.spec`) -- real subprocess spawning with a
real security surface, gated by the same missing runtime-config
registry row 2.34 (`set_config()`) already named; `async_read`/
`async_write`/`async_getdir`/`async_db_exec` (`async.spec`) -- needs
real background I/O wired through this driver's own `Scheduler`, the
same size class as the already-landed coroutine scheduler (row 2.5)
but for I/O specifically; and a final group (`ffi.spec`'s 18 names,
`develop.spec`'s remaining 12, `matrix.spec`'s 8, `jsbridge.spec`'s 3)
that each resolve to an already-stated reason (buffer type, driver-
internals-diagnostics category already scoped in row 2.36, a niche
3D-mud package, a WASM-only bridge this native driver has no analog
for) rather than a new one, checked directly against this session's
own real `.spec` fetch rather than assumed to overlap.

**Documentation updated to match:** nine new `ROADMAP.md` rows,
2.37 (`[x]`, the three built efuns, full citation trail) and 2.38-2.45
(`[ ]`, the eight deferred names/families, each with its own concrete,
source-cited reason); `COMPARISON.md`'s Phase 2 done-count (12/36 to
13/45) and its "what AMLP does not have" bullet updated to match, with
a new dated re-sweep note rather than a rewrite.

**2026-08-27 (a further session, same day): the row 2.25 method
(cross-check an already-partially-implemented efun category's real
current `.spec` file against this driver's own registered efuns, not
research candidates one at a time) applied systematically across
several more categories. Swept `core.spec` (strings/arrays/mappings/
objects/general -- all one real package in current FluffOS, not
separate `.spec` files) plus `trim.spec`, `contrib.spec`, and `ops.spec`
(confirmed to be bytecode operators, not efuns, out of scope). Found 41
real names genuinely absent from the vendored 2.9 reference; built five
small, independently-verifiable ones this session, named and scoped six
larger/harder-to-verify ones rather than building them speculatively or
dropping them silently. 789 tests passing (up from 776), all five
built efuns live-verified against the real running driver.**

**The sweep method, and what it found.** `core.spec` (fetched live,
`src/packages/core/core.spec`) was parsed for every declared efun name,
including alias forms (e.g. `"object this_interactive this_player(int
default: 1);"` declares two real callable names, `this_interactive` and
`this_player`, sharing one C implementation) -- 238 real names extracted,
cross-referenced directly against every `registerEfun("...")` call
already in `EfunTable.cpp`, not assumed. 58 names came back missing;
each was then checked against `temp/reference/fluffos-2.9-ds2.08` to
separate genuinely-new-since-2.9 gaps (this session's own real target)
from old FluffOS gaps this project already correctly deferred for other
reasons (`ed`/`ed_cmd`/`ed_start`/`get_char`/`allocate_buffer`/
`bufferp`/`read_buffer`/`write_buffer`/`cache_stats`/`dumpallobj`/
`dump_file_descriptors`/`malloc_status`/`memory_info`/`mud_status`/
`resolve`/`request_term_type`/`start_request_term_type` all already
existed in 2.9, confirmed by direct grep, not new modernization work) --
41 names cleared that bar. The same method applied to `trim.spec`
(`src/packages/trim/trim.spec`, 3 names, all 3 missing and all 3
confirmed genuinely new-since-2.9) and a lighter pass over `contrib.spec`
(most names already implemented, per this driver's own already-large
existing `pluralize`/`terminal_colour`/`repeat_string`/etc. coverage --
no new gaps found there worth a dedicated row) and `ops.spec` (confirmed
to declare bytecode operators, not real callable efuns at all, out of
scope entirely).

**Ranked by the same independent-verifiability standard as `hash()`/
`time_ns()`/`secure_random()`/`log2()`/`round()`: prefer real
identities, round-trip properties, or direct standard-library
comparison over anything needing a live current-FluffOS instance to
verify subtle behavior against.** Five names cleared that bar and were
small enough to build this same session:

- **`trim`/`ltrim`/`rtrim`** (`src/packages/trim/trim.spec`): plain
  string character-class trimming, default whitespace or an explicit
  charset. The single most airtight verification surface of anything
  built this session -- pure string identities (idempotence,
  `trim(s) == rtrim(ltrim(s))`), zero floating-point question at all
  (unlike even `log2()`/`round()`'s own standard math identities).
- **`explode_reversible`** (`src/packages/core/core.spec`): a lossless
  sibling of `explode()`, real doc's own explicit, stated guarantee
  `implode(explode_reversible(str, delim), delim) == str` for any
  non-empty `delim` -- the textbook round-trip-property verification
  style, confirmed correct even for a string made entirely of the
  delimiter (real `explode_string()`'s own "issue #968" bug-fix comment
  needed a special case for exactly that; a plain split-at-every-
  occurrence algorithm produces the correct result by construction, no
  special-casing needed on this side).
- **`call_out_walltime`**: real doc says it exists specifically because
  real `call_out()` schedules against a coarse, once-per-loop-iteration
  `current_time` rather than true wall-clock time. This driver's own
  `call_out()` was already `std::chrono::steady_clock`-based (confirmed
  by reading its own implementation before assuming anything), so this
  is a real, honest alias of the exact same code -- the distinction real
  `call_out_walltime()` exists to draw does not apply to this driver's
  architecture, named explicitly rather than silently pretending a
  byte-identical mechanism was ported.
- **`enable_wizard`/`disable_wizard`/`wizardp`**: a plain per-object
  boolean flag (`LpcObject::isWizard()`/`setWizard()`, the same shape
  as the pre-existing `isHidden()`/`setHidden()` precedent), gated on
  the same "is `current_object` interactive right now"
  (`InteractiveRegistry` membership) check `interactive()` already
  uses. Real, but currently unreachable through this driver's own
  bundled mudlib's normal play, found and worth naming precisely rather
  than glossing over: `mudlib/clone/user.c`'s own `setup()` already has
  a real, pre-existing `#ifndef __NO_WIZARDS__ enable_wizard(); #endif`
  call site, but this driver's own `ObjectManager.cpp` predefines
  `__NO_WIZARDS__` for every compile (confirmed directly), so that
  block has always compiled out -- these three efuns are real and
  correct but only exercised by this session's own temporary debug
  command so far, not by any live path through the bundled mudlib
  today. One real, concrete future connection named, not built: real
  `error_handler()` reads this exact flag to decide a wizard's own
  full-trace error message versus `DEFAULT_ERROR_MESSAGE` for an
  ordinary player -- the earlier connection-isolation session's own
  dispatch-error catch explicitly noted "this driver has no wizard/
  mortal distinction to gate on" at the time; that infrastructure now
  exists, wiring it up is a real, separate, smaller future increment.
- **`sys_network_ports`**: real shape is `({ port_#, type, port, tls })`
  per configured listening port. This driver has exactly one real port
  and no TLS, so the honest, real return is a single-element array
  matching that one real port exactly -- not a fabricated multi-port or
  TLS entry, and not silently declining to implement the efun at all
  just because this driver's own multi-port support (a real, separate,
  larger gap) is not there.

**Six real names found and deliberately deferred, each with a concrete
reason, not dropped silently:**

- `query_notify_destruct`/`set_notify_destruct` -- the efun bodies
  themselves are trivial (the same flag-accessor shape as
  `enable_wizard()` above), but building them as bare accessors without
  also wiring `destruct()`'s own real behavior (conditionally calling
  `on_destruct()`) would be a real, misleading half-feature. That
  wiring touches `VM::destructObject()`'s own critical lifecycle path,
  genuinely more than a same-session addition alongside five other
  rows without rushing a change to a sensitive path.
- GMCP/MSDP/MSP/ZMP/MXP telnet extensions (11 real names) -- confirmed
  real protocol-negotiation work in `src/net`'s telnet layer, the same
  size class as TLS/WebSocket (rows 2.13/2.14), not a bounded
  single-session efun addition.
- UTF-8/charset conversion plus a real `buffer` value type (10 real
  names) -- confirmed this driver's own `Value` variant has no buffer
  kind at all; a real new value type touching the parser, `Value`,
  `sprintf`/`typeof`/equality, and save/restore, before any of these
  efun bodies have anywhere real to operate on. Reconfirmed, not
  re-derived: an earlier session's own research pass already named this
  as a larger candidate.
- `set_config` -- needs the same real, currently-missing ~50-entry
  runtime config registry this driver's own already-implemented
  `get_config()` already honestly admits it lacks in its own comment
  (a single-index stub, real index range unimplemented) -- the
  registry is the real prerequisite, not the setter body itself.
- `defer()` -- real per-LPC-frame `finally`/RAII semantics guaranteed
  on every real exit path of the *calling* function, needing a genuine
  new hook threaded through `VM::run()`'s own frame lifecycle to run an
  LPC closure specifically. Real VM frame-lifecycle feature, not a
  three-line efun the way rows 2.26-2.30 were.
- Driver-internals diagnostics (`dump_trace`/`trace_start`/`trace_end`/
  `function_profile`/`clear_debug_level`/`debug_levels`, confirmed
  genuinely new-since-2.9; `cache_stats`/`malloc_status`/`memory_info`/
  `mud_status`/`dumpallobj`/`dump_file_descriptors`, confirmed
  pre-existing 2.9 gaps, not modernization work) -- expose this
  driver's own specific memory allocator/malloc-debug/object-table
  internals real FluffOS's own `jemalloc`/`debugmalloc` machinery has
  no equivalent structure for at all; not independently portable
  behavior, would mean fabricating driver-internal statistics this
  codebase has no real source for.

**12 new regression tests (789 total, up from 776):** 4 for
`trim`/`ltrim`/`rtrim` (default-whitespace both-ends stripping;
`ltrim`/`rtrim` each stripping only their own end plus the
`trim == rtrim(ltrim(...))` identity; a custom charset stripping
exactly those characters, whitespace left alone once given; idempotence
on an already-trimmed string); 3 for `explode_reversible` (the real
doc's own worked example; the round-trip identity across five varied
inputs including a leading/trailing/adjacent-delimiter mix and a
string made entirely of the delimiter; throws on an empty delimiter);
2 for `call_out_walltime` (accepts the real argument shape and returns
a handle; actually fires via the same scheduler `call_out()` uses); 3
for `enable_wizard`/`disable_wizard`/`wizardp` (a real no-op for a
non-interactive object; toggling the flag on a real interactive
object; `wizardp()` reading the flag on an explicit argument); 1 for
`sys_network_ports` (the single real entry matches the real shape and
this exact process's own real configured port).

**Live-verified against the real running driver** (`./build/amlp
etc/driver.cfg`, real TCP sessions, the same real bundled mudlib and
gatehouse login flow prior sessions used), via real `eval` calls for
four of the five: `trim("  hello world  ")` returned `"hello world"`;
`ltrim("xxhixx", "x")`/`rtrim("xxhixx", "x")` returned `"hixx"`/
`"xxhi"`; `implode(explode_reversible("a,,b,", ","), ",")` round-tripped
back to `"a,,b,"` exactly, `sizeof(...)` returned `4`;
`call_out_walltime("eval", 0)` returned a real handle; `sys_network_ports()`
returned `({ ({ 1, "telnet", 1122, 0 }) })`, `1122` being this exact
process's own real configured port. `enable_wizard`/`disable_wizard`/
`wizardp` needed a different real vehicle than `eval`, found and worth
naming precisely: `eval`'s own code always runs as a fresh, throwaway
`/tmp_eval_file` object, which is never itself `InteractiveRegistry`-
registered, so `enable_wizard()`'s own real "only works on a currently
interactive `current_object`" guard correctly, silently no-ops there --
confirmed live (`wizardp(this_player())` stayed `0` through an
`enable_wizard()`/`wizardp()` `eval` sequence, zero driver-log errors,
exactly the expected real no-op outcome, not a bug). A temporary
`wiztest` debug command was added directly to `mudlib/clone/user.c`
instead (real `current_object` there genuinely is the interactive
player object), reverted via `git checkout` immediately after,
confirmed clean via `git diff --stat`: `wizardp(this_object())`
before/after `enable_wizard()`/`disable_wizard()` returned `0`/`1`/`0`
exactly, zero errors in the driver's own log throughout. Test-account/
character files created during all of this session's verification
passes (`sweepcheck27`/`SweepChar`, `wiztest27`/`WizTestChar`,
`portcheck27`/`PortCheckChar`) deleted afterward, matching this
project's own established cleanup precedent.

**Documentation updated to match:** eleven new `ROADMAP.md` rows,
2.26-2.30 (`[x]`, the five built efuns, full citation trail in each
row's own cell) and 2.31-2.36 (`[ ]`, the six deferred names/families,
each with its own concrete, source-cited reason for deferral rather
than a vague "too big"); `COMPARISON.md`'s Phase 2 done-count (7/25 to
12/36) and its "what AMLP does not have" bullet updated to match, with
a new dated re-sweep note rather than a rewrite.

**2026-08-27 (a further session, same day): `json_encode()`/`json_decode()`
(row 2.17) re-examined on request, specifically to check whether their
own real formatting semantics were actually specified in source rather
than assumed unspecified. Found something stronger: they are not real
current-FluffOS efuns at all. Fell back to fresh research, found
`log2()`/`round()` (new row 2.25), both confirmed genuinely
new-since-2.9 and built the same way as `hash()`/`time_ns()`/
`secure_random()`. 776 tests passing (up from 773), live-verified
against the real running driver via real `eval` calls.**

**The `json_encode`/`json_decode` re-examination, and its real verdict.**
The prior session's own ranking deferred this row because its real
formatting choices (mapping key order, float precision) seemed
unverifiable without a live current-FluffOS instance to check subtle
behavior against. This session checked the premise directly rather than
accepting it: is that formatting genuinely unspecified, or does real
source actually pin it down? The check found something more definitive
than either answer. `src/svalue_json.cc` -- the one file path in
`github.com/fluffos/fluffos@master`'s own tree whose name suggested a
real svalue-to-JSON bridge -- is a genuinely empty placeholder (`git`
blob size `0`, the well-known empty-blob SHA `e69de29b...`, confirmed
directly via the GitHub API tree listing, not assumed from the path
existing). Every real package `.spec` file was then fetched and checked
in full (`core.spec` plus all 20 other `src/packages/*/*.spec` files,
`async`/`compress`/`contrib`/`crypto`/`db`/`develop`/`dwlib`/`external`/
`ffi`/`jsbridge`/`math`/`matrix`/`mudlib_stats`/`ops`/`parser`/`pcre`/
`sha1`/`sockets`/`trim`/`uids`): zero declare `json_encode`,
`json_decode`, or any `json`-named efun anywhere. `docs/efun/` (425 real
doc pages, the same tree `hash()`/`time_ns()`/`secure_random()` were all
confirmed against) has zero pages for either name under any category.
**Verdict: `json_encode()`/`json_decode()` are not real current-FluffOS
efuns at all** -- the formatting-ambiguity concern that deferred this
row previously never actually arises, because there is no real efun
target to match in the first place. This is a stronger, more concrete
finding than "hard to verify," and this row's own `ROADMAP.md` cell is
updated to say so precisely rather than repeat the older, softer framing.

**What real current FluffOS actually has instead, found along the way:**
`json2o`/`o2json`, a real, documented pair of standalone CLI binaries
(`src/main_json2o.cc`/`src/main_o2json.cc`, real docs fetched live at
`docs/cli/json2o.md`/`o2json.md`) that convert an on-disk `.o` save file
to/from a specific JSON schema (`{"program_name", "variables":
[{"name", "value": {"type", "value"}}]}`, every `type` value enumerated
in the doc: `"int"`, `"float"`, `"string"`, `"array"`, `"mapping"`,
`"buffer"`). A real, ops-facing build-time tool operating on save
files, never an LPC-callable efun a running mudlib invokes -- confirmed
structurally different from what row 2.17's own title ever described,
not a substitute worth building in its place this session (a different
row, a different real gap, and its own real formatting schema is
already fully specified if it is ever picked up on its own merits
later).

**Falling back to fresh research, same discipline as the `time_ns()`/
`secure_random()` session.** Surveyed the remaining real current-FluffOS
efun doc categories this project's own research had not yet fully swept
(`floats`, `general`, `objects`, `internals`, and the other categories
outside the ones `hash()`/`time_ns()`/`secure_random()` already covered),
cross-checked against this driver's own already-implemented efun set
directly in `EfunTable.cpp` rather than guessed. Also checked, and ruled
out for this session specifically: `http_get`/`http_post` (row 2.18)
confirmed not real current-FluffOS efuns either (only vendored
third-party `libevent`/`libwebsockets` internals turned up, no
LPC-exposed HTTP client efun anywhere in the real source); `recompile_object()`
(row 2.21) remains the large, multi-session item an earlier research
session already scoped it as (live-frame-on-call-stack detection,
by-name variable-layout migration across every clone, stale
function-pointer invalidation); `assert_equal`/`assert_throws` (row
2.22) remains confirmed, from an earlier session, not real
current-FluffOS efun surface either.

**`log2()`/`round()`: real signatures and semantics, confirmed from
source, not guessed.** This driver's own already-implemented math efun
set (`sin`/`cos`/`tan`/`asin`/`acos`/`atan`/`sqrt`/`log`/`log10`/`pow`/
`exp`/`floor`/`ceil`, all confirmed present directly in `EfunTable.cpp`
before assuming a gap) was checked against the real current
`src/packages/math/math.spec` (fetched live): two real declared efuns
were missing, `float log2(float|int);` and `float round(float);`. Both
confirmed genuinely new since 2.9 (`temp/reference/fluffos-2.9-ds2.08`
has no `log2`/`round`/`f_log2`/`f_round` anywhere in it at all -- only
`log()`/`log10()`/`floor()`/`ceil()` existed there). Real bodies
(`src/packages/math/math.cc`, fetched live): `f_log2()` accepts an int
or a float (an int promoted to float first), `error("math: log2(x)
with (x <= 0.0)\n");` on a non-positive argument, otherwise real C
library `log2()`; `f_round()` is `"sp->u.real = round(sp->u.real);"`,
plain C library `round()`, round-half-away-from-zero, no domain
restriction at all. `round()`'s own real spec is float-only, unlike
`log2()`'s `float|int` -- this driver promotes int to float for
`round()` too, deliberately, matching its own already-established
`floor()`/`ceil()` precedent (both real-float-only-in-spec, already
ported leniently the same way) rather than introducing a new,
inconsistent strict-float-only special case; named here as a local-
consistency choice, not a real-signature deviation.

**Built.** Both registered in `EfunTable.cpp`: `log2()` right after
`log10()`, `round()` right after `ceil()`, reusing the same `asFloat()`
helper and domain-guard shape this driver's other math efuns already
use. Zero new dependency: `<cmath>`, already included and already used
by every other math efun here.

**3 new regression tests** (`test_lexer.cpp`): `log2()` checked against
plain standard math identities (`log2(1)==0`, `log2(8)==3`,
`log2(1024)==10`, plus the `2^log2(x)==x` round-trip identity,
independent of any particular implementation's own rounding) and throws
on `x<=0` (both `0.0` and a negative value); `round()` checked
round-half-away-from-zero across positive/negative halves and
non-halves, each cross-checked directly against `std::round()` rather
than a hand-picked expected value -- no fixed test vector risk the way
`json_encode()`'s own formatting questions carried, matching the
independent-verifiability bar this session was asked to hold every
candidate to.

**Live-verified against the real running driver** (`./build/amlp
etc/driver.cfg`, a real Python TCP client, the same real bundled mudlib
and gatehouse login flow prior sessions used), via real `eval` calls:
`eval return log2(8.0);` / `eval return log2(1024.0);` returned
`3.000000` / `10.000000`; `eval return log2(0.0);` / `eval return
log2(-3.0);` both errored, the driver's own log showing the exact real
error text (`"math: log2(x) with (x <= 0.0)"`) and confirming, as a
useful side effect, that the connection-isolation fix from an earlier
session still holds -- the connection stayed open and every subsequent
`eval` in the same session kept working normally; `eval return
round(2.5);` / `round(-2.5);` / `round(2.4);` / `round(-2.4);` returned
`3.000000` / `-3.000000` / `2.000000` / `-2.000000`, matching
round-half-away-from-zero exactly. Driver's own log showed no
unexpected errors throughout. Test-account/character files created
during verification (`mathcheck27`, `MathCheckChar`) deleted afterward,
matching this project's own established cleanup precedent.

**Documentation updated to match:** `ROADMAP.md` row 2.17 appended with
the definitive re-examination finding (not rewritten); new row 2.25
(`log2()`/`round()`), `[x]`, full citation trail in its own cell;
`COMPARISON.md`'s Phase 2 done-count (6/24 to 7/25) and its "what AMLP
does not have" bullet updated to match, with a new dated re-sweep note.

**2026-08-27 (a further session, same day): the prior session's top two
ranked modernization candidates, `time_ns()`/`perf_counter_ns()` and
`secure_random()`, both real and confirmed absent from the vendored 2.9
reference, confirmed directly against real current FluffOS source
(not guessed) and built. New `ROADMAP.md` rows 2.23/2.24. 773 tests
passing (up from 767), both live-verified against the real running
driver via real `eval` calls.**

**`time_ns()`/`perf_counter_ns()`: real signatures and semantics,
confirmed from source, not guessed.** `src/packages/core/core.spec`
(fetched live from `github.com/fluffos/fluffos@master`, the same
GitHub-API-tree-plus-raw-fetch method the SETJMP-vs-exception research
used last session): line 371 `int perf_counter_ns();`, line 373
`int time_ns();`, both zero-argument. Real bodies,
`src/packages/core/time.cc:9-36`: `f_time_ns()` is
`std::chrono::system_clock::now()`, `duration_cast<nanoseconds>` of
`time_since_epoch()`, pushed as the return value -- wall-clock
nanoseconds since the Unix epoch. `f_perf_counter_ns()` (non-Windows
branch, the only real scope here) is the same cast applied to
`std::chrono::high_resolution_clock::now()` instead. Real FluffOS's own
testsuite (`testsuite/single/tests/efuns/time_ns.lpc`/
`perf_counter_ns.lpc`, fetched live, not assumed) confirms the shape
directly: `time_ns()` is asserted against a real epoch-nanosecond lower
bound (`ASSERT(x > 1685382080000000);`, real June-2023 epoch
nanoseconds); `perf_counter_ns()` is only ever asserted monotonic
between two successive calls (`ASSERT(b >= a);`), never against a fixed
or epoch-relative value. One real, worth-naming inconsistency found in
the real source itself, not this driver's own: `core.spec`'s own
comment on `perf_counter_ns()` ("return highest resolution clock in
platform dependent unit") is stale against the real implementation,
which always converts to nanoseconds regardless of platform -- ported
to match the real *code*, not the stale comment. Both efuns confirmed
absent from `temp/reference/fluffos-2.9-ds2.08` entirely (no
`time_ns`/`perf_counter_ns` anywhere in that tree), genuinely new since
2.9.

**Built to match, zero new dependency.** Both registered in
`EfunTable.cpp` right after `real_time()`, reusing `std::chrono`
(already `#include`d, already used by this driver's other time-related
efuns) exactly the way real FluffOS's own `time.cc` does: `time_ns()`
via `std::chrono::system_clock::now()`, `perf_counter_ns()` via
`std::chrono::high_resolution_clock::now()`, both
`duration_cast<nanoseconds>(now.time_since_epoch()).count()`, ported
verbatim including real FluffOS's own choice of `high_resolution_clock`
rather than `steady_clock` for `perf_counter_ns()` -- not "improved" to
a textbook-stricter monotonic clock, since that would diverge from what
real FluffOS's own shipped code actually does (this project's own
established "port the real behavior, not just the sensible version"
discipline).

**3 new regression tests** (`test_lexer.cpp`): `time_ns()` bracketed
between two independent `std::chrono::system_clock::now()` measurements
taken immediately before/after the LPC call (mirrors
`testRealTimeReturnsCurrentUnixTime`'s own exact shape), plus the real
testsuite's own epoch-nanosecond lower bound reproduced directly;
`perf_counter_ns()` called twice in one LPC function, asserting the
second is `>=` the first (mirrors the real testsuite's own exact
assertion); a real `std::this_thread::sleep_for(50ms)` between two
`time_ns()` calls, asserting the returned delta falls between a 30ms
floor (scheduler-jitter tolerance) and a 1s ceiling -- proving the
value genuinely advances at real nanosecond resolution across a real
measured interval, the "measured sleep interval reflected correctly"
verification this session was asked for specifically.

**`secure_random(int n)`: real signature and semantics, confirmed from
source, not guessed.** `core.spec:66`: `int secure_random(int);`, one
argument. Real `secure_random_number()` (`src/base/internal/port.cc:
32-44`, called by `f_secure_random()`, `efuns_main.cc`), found by
searching for the real entropy-source keywords the real doc promised
("/dev/urandom") after `core.spec`'s own declaration did not point
directly at an implementation file: on Linux/OSX (this driver's own
only real target), `static std::random_device rd("/dev/urandom");
std::uniform_int_distribution<int64_t> dist(0, n - 1); return dist(rd);`
-- `std::random_device` used DIRECTLY as the distribution's own engine,
drawing fresh entropy from `/dev/urandom` on every single call, not
seeding a separate deterministic PRNG the way this driver's own
pre-existing `random()` (and real FluffOS's own equivalent
`random_number()`, same file) does. `n <= 0` returns 0, the same
`uniform_int_distribution`-UB guard real `secure_random_number()` has.
Real doc (`docs/efun/numbers/secure_random.md`, fetched live): "Return
a cryptographically secure random number from the range
[0 .. (n - 1)] (inclusive). On Linux & OSX, this function explicitly
use randomness from /dev/urandom."

**One real, worth-naming divergence from this session's own starting
assumption, corrected against confirmed source rather than followed
blindly.** This session's own brief suggested reusing this driver's
already-linked `libcrypto`/OpenSSL dependency (added for `hash()`,
row 2.16) via `RAND_bytes()`. Confirmed directly against real current
source that this is not what real FluffOS actually does: real
`secure_random_number()` never touches OpenSSL at all, only
`std::random_device`. Matching the real implementation exactly took
priority over the suggested approach -- ported real FluffOS's own
actual mechanism verbatim instead, named here explicitly rather than
silently substituting the "obvious-looking" OpenSSL route without
flagging the divergence from what was asked.

**Built.** `secure_random` registered in `EfunTable.cpp` right after
`random()`, same `<random>` include already in use there, real
implementation ported verbatim as described above (`libstdc++`'s own
`"/dev/urandom"` token, confirmed this driver already builds against
`libstdc++` in this environment).

**3 new regression tests** (`test_lexer.cpp`): `n <= 0` returns 0
(mirrors `testRandomOfNonPositiveArgumentIsZero`); stays within
`[0, n)` across 200 draws (mirrors `testRandomStaysWithinZeroToN
ExclusiveAcrossManyDraws`, and matches real
`testsuite/single/tests/efuns/secure_random.lpc`'s own exact structural
range-check shape, fetched live: `ASSERT(secure_random(5) >= 0);
ASSERT(secure_random(5) < 5);` in a loop); 50 draws from a wide range
(`secure_random(1000000)`) require at least 45 distinct values, a
coarse tripwire against a fixed-seed/deterministic regression --
verified structurally throughout, as this session's own brief asked
for, never against a fixed test vector, since real cryptographically
secure randomness has no fixed expected output by design.

**Live-verified against the real running driver** (`./build/amlp
etc/driver.cfg`, a real Python TCP client, the same real bundled
mudlib and gatehouse login flow the connection-isolation session just
above used), via real `eval` calls this time rather than a temporary
debug command, since both new efuns are directly callable from `eval`
with no mudlib change needed: `eval return time_ns();` returned a real,
plausible current epoch-nanosecond value (`1787853390401282579`, ~=
2026-08-27, matching this environment's own real date); `eval return
sprintf("%d %d", perf_counter_ns(), perf_counter_ns());` returned a
correctly non-decreasing pair from two calls in the same expression; a
real ~1 second sleep between two separate `eval return time_ns();`
calls (network round trip included) showed up as a closely matching
real nanosecond delta in the driver's own returned values (`1.858s`
driver-measured against `1.875s` Python-side wall-clock, the small gap
fully accounted for by round-trip/processing overhead); five real
`eval return secure_random(10);` calls returned genuinely varied values
(9, 2, 9, 1, 3), all within range; `eval return secure_random(0);`
returned `0`. Driver's own log showed no errors throughout. Test-
account/character files created during verification
(`livecheck27`/`livecheck27b`, `livecheckchar`/`livecheckchar2`)
deleted afterward, matching this project's own established cleanup
precedent.

**Documentation updated to match:** new `ROADMAP.md` rows 2.23
(`time_ns()`/`perf_counter_ns()`) and 2.24 (`secure_random()`), both
`[x]`, full citation trail in each row's own cell; `COMPARISON.md`'s
Phase 2 done-count (4/22 to 6/24) and its "what AMLP does not have"
bullet updated to match, with a new dated re-sweep note rather than a
rewrite.

**2026-08-27 (a further session, same day): the connection-isolation gap
row 3.9's own combat pass found and deferred (an uncaught command error
closes the whole connection instead of isolating just that command) is
now fixed. Real semantics confirmed from source on both sides of the
2.9-vs-current line before writing any code, not assumed; the fix
itself was small and bounded, built the same session. 767 tests passing
(up from 764), live-verified against the real running driver and the
real bundled mudlib.**

**Real FluffOS recovery semantics, confirmed from source, both sides.**
Vendored 2.9 ds2.08 reference (`temp/reference/fluffos-2.9-ds2.08/`):
`backend()` (`backend.c:113-116`) calls `save_context(&econ)` and
`SETJMP(econ.context)` exactly ONCE, before its own `while(1)` command
loop, establishing a single global recovery point for the entire process
lifetime -- `process_user_command()` (`comm.c:1829`) itself has no
`SETJMP` of its own, and neither does `parse_command()`
(`add_action.c:425`), which calls `apply()` on the matched verb function
directly. Per-command isolation in 2.9 is therefore an implicit side
effect of that single top-level context, not a dedicated mechanism: an
uncaught `error()` (`simulate.c`'s `error_handler()`) always `LONGJMP`s
back to the same one `econ`, `restore_context()` (`interpret.c:5741`)
manually resets the VM's own `sp`/`csp`/`cgsp` pointers back to their
saved position and frees any leftover `ref_t` references, and the outer
`while(1)` loop simply continues -- the connection/socket/`interactive_t`
are never touched by this path at all. `error_handler()` itself
(`simulate.c:1676-1800`) always gives the player *something*:
`add_message_with_location()` shows the full trace to a wizard
(`O_IS_WIZARD`), or the config-driven `DEFAULT_ERROR_MESSAGE` otherwise;
a `current_heart_beat` object gets its heart beat disabled if the error
fired during one, specifically to stop an infinite per-tick error loop;
`too_deep_error`/`max_eval_error` are reset so the *next* command is not
permanently stuck. One real, confirmed-live piece of defensive ordering
worth naming: `call_function_interactive()` (`comm.c:2073-2191`, the
`input_to()` callback dispatcher) clears `i->input_to = 0` BEFORE
invoking the callback, not after -- so an uncaught error mid-callback
can never leave a stale/dangling `input_to` registration, by
construction, not by any special-casing in the error path itself.

**Confirmed changed since 2.9: current FluffOS moved from `SETJMP`/
`LONGJMP` to real C++ exceptions, and from one implicit global boundary
to explicit, dedicated per-call recovery.** Fetched directly from
`github.com/fluffos/fluffos` (`master`, via the GitHub API tree listing
plus raw file fetches, not summarized/guessed): `save_context`/
`restore_context`/`pop_context`/`error_context_t` all still exist with
the same names and the same job (`vm/internal/base/interpret.cc:5553-
5607`), but `error_handler()` (`vm/internal/simulate.cc:2369-2489`) now
ends every path with `throw("error handler")`/`throw("error handler
error")` instead of `LONGJMP`, and the real per-command boundary is no
longer one global `SETJMP` in `backend()` (confirmed: current
`backend.cc` has zero `SETJMP`/`error_context`/`try`/`catch` of its own
at all) -- it is now `safe_parse_command()` (`packages/core/
add_action.cc:488-497`, a fresh `error_context_t econ`, `save_context(&econ)`,
`try { parse_command(str, ob); } catch (const char*) { restore_context(&econ);
} pop_context(&econ);`), called from `process_input()` (`comm.cc:612-644`)
in place of 2.9's bare `parse_command()` call. `process_input()`'s own
mudlib-facing `APPLY_PROCESS_INPUT` apply is likewise now wrapped in
`safe_apply()` (`vm/internal/apply.cc:398-422`, the identical `save_context`/
`try`/`catch(const char*)`/`restore_context`/`pop_context` shape) rather
than a bare `apply()` -- current FluffOS wraps every individual risky
call explicitly and separately, not one call relying on an outer
boundary set up elsewhere. `restore_context()`'s own unwind logic is
functionally the same as 2.9's, plus one real hardening: a guard against
`sp` already being below the saved mark (issue #1014, a broken-stack-
accounting bug 2.9's own unconditional `pop_n_elems(sp - econ->save_sp)`
had no defense against). `error_handler()`'s own player-facing message
logic (`_error_handler()`, `vm/internal/simulate.cc:2324-2364`) is
otherwise unchanged from 2.9's: same wizard/`DEFAULT_ERROR_MESSAGE`
split, same heart-beat-disable-on-error behavior. `input_to`'s own
clear-before-invoke ordering is preserved identically in current
`comm.cc`. **Net effect for this driver's own purposes: real current
FluffOS's own error recovery is functionally the same outcome as 2.9's
(one command's error never closes the connection), but the mechanism it
now uses -- real C++ exceptions around individually-wrapped calls -- is
the exact idiom this driver already uses everywhere else for
`LpcRuntimeError`, not something foreign to port in.**

**Why this driver's own current behavior diverges: a deliberate
simplification revisited, not a structural limitation.** `Server::
dispatchLine()` (`Server.cpp:237`, this driver's own single-function
collapse of real `process_input()`/`parse_command()`) had zero internal
`try`/`catch` of its own; the ONLY catch in the whole per-command path
was `Server::handleConnection()`'s own per-line loop (`Server.cpp:497-
537`, before this fix), which on any `std::exception` called
`conn.markClosed()` and `break`, closing the connection and dropping any
further already-buffered lines in that same poll batch. Confirmed this
was never structural: this driver's own VM (`VM.cpp`) already uses RAII
guards throughout (`ObjectFrameGuard`, `CommandGiverGuard`, `OriginGuard`,
and `run()`'s own comment: "RAII rather than an explicit pop before
every return: run() has several return points plus exception unwinding
... a destructor is the only pop that reliably covers all of them") --
meaning this driver's own equivalent of real `restore_context()`'s whole
job (unwinding `sp`/`csp`/`cgsp` back to a clean state) is already
handled automatically by ordinary C++ stack unwinding on every single
call, everywhere, not just at one boundary. `evalCost_` needs no reset
either: a plain counter, already unconditionally reset at the top of
every `dispatchLine()` call (`vm.resetEvalCost()`) regardless of whether
the previous call threw. There was therefore no `restore_context()`-
shaped gap to fill at all -- the only real gap was the per-line catch's
own choice to treat any uncaught error as connection-fatal, an early
simplification that was never revisited once this driver's exception-
safety guarantees were solid elsewhere in the codebase.

**The fix.** `Server::handleConnection()`'s per-line catch
(`Server.cpp`) no longer calls `conn.markClosed()`: it logs the real
exception detail to the driver's own log (unchanged), reports a generic
message to the player via `deliverToConnection()` (itself wrapped in its
own nested `try`/`catch`, since a snoop relay's own `receive_snoop()`
apply could itself throw -- an error while reporting an error must not
undo the fix by escaping uncaught), then `continue`s to the next
already-buffered line in this same poll batch instead of `break`ing out
of the loop. This driver has no wizard/mortal distinction to gate a
fuller message on the way real `_error_handler()` does (`privs()` only,
no full uid/euid hierarchy, per `COMPARISON.md`'s own accounting), so
every player gets the same generic message; the real exception detail
stays server-side, matching real `DEFAULT_ERROR_MESSAGE`'s own intent of
not leaking internal detail to an ordinary connection. A real, adjacent
config-driven `DEFAULT_ERROR_MESSAGE`-equivalent (configurable per-mud
rather than hardcoded) is a genuine, separate, smaller future increment,
named here rather than silently built in scope-creeping fashion this
session.

**3 new regression tests (767 total, up from 764):**
`testDispatchErrorInOneCommandDoesNotCloseTheConnection` (one command's
uncaught error leaves `conn.closed()` false); `testDispatchErrorInOne
CommandReportsAGenericMessageToThePlayer` (the player receives exactly
the generic message over the real socket, not raw internal exception
text); `testCommandAfterADispatchErrorStillRunsNormallyProvingVmState
NotCorrupted` (a second command dispatched immediately after the one
that threw still runs its own side effect correctly, proving this
driver's RAII-based VM state was never corrupted by the escaped
exception). One pre-existing test's own comment was stale after this
fix and corrected rather than left describing removed behavior:
`testUncaughtDispatchErrorStillFiresNetDeadUnlikeExplicitClose`, renamed
`testFireNetDeadFiresForAnyMarkClosedConnectionWithValidBoundObject`,
narrowed to describe what it actually still proves (`fireNetDeadIfLink
Dead()`'s own correctness for any `markClosed()` connection with a valid
`boundObject()`, independent of why it was closed) rather than
`handleConnection()`'s own now-outdated old sequence. The original 764
re-run unchanged.

**Live-verified against the real running driver and the real bundled
`mudlib/`** (`./build/amlp etc/driver.cfg`, a real Python TCP client): a
temporary `boomtest` debug command added to `mudlib/clone/user.c`
(`add_action("boomtest", "boomtest")` in `setup()`, calling a genuinely
undefined efun), reverted via `git checkout` immediately after,
confirmed clean via `git diff --stat`. A fresh account/character created
and walked through the real gatehouse login flow over a real TCP
connection: `say hello before` (control, before the error) worked
normally; `boomtest` produced exactly `"Error while processing your
command.\n"` back to the player, and the driver's own log showed the
real exception detail (`connection fd=4 input handling failed (command
isolated, connection stays open): /clone/user::boomtest(): undefined
function or efun: totally_undefined_efun_for_live_verification_only`);
the connection stayed open and fully functional afterward -- `say hello
after` and `who` both worked normally, `who` correctly still listing the
same character as connected with idle 0 -- and the driver process itself
was confirmed still running throughout via `ps`. Test-account/character
files created during verification (`boomtestverify`/`boomtestverify2`/
`boomtestverify3`, `boomchar2`/`boomchar3`) deleted afterward, matching
this project's own established cleanup precedent.

**Out of scope, deliberately:** a configurable `DEFAULT_ERROR_MESSAGE`-
equivalent (a real, separate, smaller future increment, see above); a
wizard/mortal message-detail distinction (this driver has no such flag
today, `COMPARISON.md`'s own accounting); per-apply-granular recovery
matching current FluffOS's own `safe_apply()`-everywhere shape exactly
(this driver's single per-line `try`/`catch` around the whole
`dispatchLine()` call already produces the same observable outcome --
one command's error never escapes past that command -- with less
surface area to get wrong, and was preferred for that reason, not
because the finer-grained shape was rejected on its merits).

**Documentation updated to match:** `ROADMAP.md` row 3.9's own cell
(where this gap was originally scoped and deferred) appended with the
fix, the full citation trail above, and the live-verification account,
rather than rewritten.

**2026-08-27: first pass at modernizing AMLP against *current* FluffOS
specifically, not just the vendored 2.9 ds2.08 reference this project
otherwise cites throughout -- research first, then one bounded
implementation. `hash(string algo, string str)` (`ROADMAP.md` row 2.16)
landed: real current FluffOS's own single string-package efun, confirmed
via its live documentation site (`docs/efun/strings/hash.md`) and,
separately, confirmed genuinely absent from the vendored 2.9 reference
(no LPC-visible `hash()` efun anywhere in
`temp/reference/fluffos-2.9-ds2.08`, only an unrelated internal
`whashstr()` C helper sharing the file name `hash.c`/`hash.h`) -- real
evidence this driver is behind current FluffOS specifically, a gap this
project's usual corpus-citation discipline (checking vendored mudlib
corpora, all of which predate this efun) would never surface on its own.**

**Why this row, over the other open Phase 2 items.** TLS (2.13),
WebSocket framing (2.14), and `recompile_object()`-style hot code reload
(researched this session, not in `ROADMAP.md` as a titled row at all)
were all considered and set aside as too large for one session: TLS/
WebSocket need real I/O-path surgery in `src/net`; `recompile_object()`
needs live-frame-on-call-stack detection, by-name variable-layout
transfer across every clone, and stale-function-pointer invalidation --
a multi-session feature, not a bounded slice. `json_encode`/`json_decode`
(2.17) was the other real candidate, same "genuinely new since 2.9,
zero corpus evidence" shape as `hash()`, but was set aside in `hash()`'s
favor specifically because this driver cannot run a real current FluffOS
instance to verify subtle type-mapping/formatting choices against (mapping
key order, float formatting, escaping) the way this project's own
discipline otherwise insists on before calling something done --
`hash()`'s correctness, by contrast, is independently, exactly verifiable
against standard NIST/RFC test vectors with no live reference driver
needed at all, confirmed directly against Python's own `hashlib` before
writing any test.

**Real semantics, confirmed from the real current docs, not inferred**:
signature `string hash(string algo, string str)`, case-insensitive
algorithm-name matching, unknown algorithm throws (real
`"hash() unknown hash type: %s"`), real algorithm list is md4/md5/
ripemd160, md2/mdc2 (OpenSSL 1.x-2.x only), sha1/sha224/sha256/sha384/
sha512, sha3-224/256/384/512, blake2s256/blake2b512, sm3 -- notably no
`bcrypt`, correcting this row's own original 2026-08-21 title
("SHA-256/SHA-512/MD5/bcrypt/BLAKE2"), which had invented a `bcrypt`
algorithm name real `hash()` never actually had.

**Built:** `hash()` registered in `EfunTable.cpp` via OpenSSL's EVP
digest interface (`EVP_get_digestbyname`/`EVP_DigestInit_ex`/
`EVP_DigestUpdate`/`EVP_DigestFinal_ex`), lowercased algorithm name
before lookup, lowercase hex-string output. New dependency: `libcrypto`,
added to `src/efun/CMakeLists.txt` via `pkg_check_modules`, the same
pattern already used there for `libpcre2-8`/`sqlite3` -- confirmed
available in this environment (OpenSSL 3.5.7) before adding it, not
assumed. Two real, confirmed-live gaps against the full algorithm list,
both probed directly against a standalone EVP test program before
writing any driver code: `md2`/`mdc2` are absent outright (removed
upstream in OpenSSL 3.x, matching the real doc's own compatibility
note) and `md4` resolves a name but fails at digest-init time (moved to
OpenSSL 3.x's "legacy" provider, not loaded by default in this build) --
both distinguished in the implementation from a genuinely unknown
algorithm name (still a real error either way, just a different one).

**3 new regression tests (764 total, up from 761):** known digests for
several algorithms cross-checked against the real doc's own worked
example (`md5("Something")` = `"73f9977556584a369800e775b48f3dbe"`)
plus independent NIST/RFC test vectors on `sha1`/`sha256`/`sha512`
(`"abc"` and `""`), each verified against Python's own `hashlib`
independently before writing the test, not derived from this driver's
own output (one real transcription bug this caught during development:
the initial `sha1("abc")` vector in the test was missing its own final
hex digit, `hashlib` cross-check caught it before the fix was trusted).
Also: case-insensitive algorithm-name matching (`"sha256"` and
`"SHA256"` produce the same digest); throwing on a genuinely unknown
algorithm name. The original 761 re-run unchanged.

**Build verified:** `cmake -B build -S .` (picks up the new `libcrypto`
`pkg_check_modules` line cleanly) and `cmake --build build -j4` both
clean, `ctest --test-dir build --output-on-failure` 100% passing (764/764).

**Documentation updated to match:** `ROADMAP.md` row 2.16 marked `[x]`
(partial) with the full citation trail above; `COMPARISON.md`'s Phase 2
done-count (3/22 to 4/22), its "what AMLP does not have" bullet, its
feature-by-feature table's hash/JSON row, and a new dated re-sweep note
all updated to match, not left stale.

**Out of scope, deliberately:** TLS/WebSocket (2.13/2.14), full hot
reload / `recompile_object()` (not a titled row, researched this session
only), `json_encode`/`json_decode` (2.17, same shape as this row but
harder to verify without a live current-FluffOS reference), and
`bcrypt` specifically (a real, separate self-motivated want over
`crypt()`'s own dated salting, not a real `hash()` algorithm at all).

**2026-08-25 (a further session, same day): the array-form `call_other()`
gap the previous session's broader pass found (real FluffOS's own
`object *` first-argument form for `call_other()`/`->`, not supported
by this driver) fixed, real semantics confirmed against the actual
interpreter-level implementation before writing any code. 5 new
regression tests (761 total, up from 756), live-verified against the
real driver and the real AetherMUD mudlib specifically: the room-reset
error is gone, and `reinitiate()`'s own real intended behavior (moving
every object in a room out and back in) confirmed actually working, not
just non-erroring.**

**Real semantics, confirmed from source, not inferred.** Read real
`efuns_main.c`'s own `f__call_other()` directly: `arg[0].type ==
T_ARRAY` dispatches to `call_all_other(arg[0].u.arr, funcname, num_arg -
2)` (`interpret.c`), whose full body was read line by line before
writing anything:

- **Result is a real array, same size as the input, index-aligned in
  the same order** (`allocate_array(size = v->size)`, `vptr`/`rptr`
  incrementing together) -- not a "drop the misses" shape.
- **Every unfilled slot defaults to real `allocate_array()`'s own plain
  `const0`** (`array.c`: `while (n--) p->item[n] = const0;`), an
  ordinary, subtype-less int 0 -- confirmed distinct from the
  T_UNDEFINED-tagged `const0u` the prior session's sprintf fix had to
  special-case (`main.c`: only `const0u.subtype = T_UNDEFINED;`).
- **A destructed array element is silently skipped, not an error**
  (`if (ob->flags & O_DESTRUCTED) continue;`), its own slot staying at
  the default 0.
- **A missing function on one element is likewise silently skipped, not
  an error** (`if (apply_low(...))` false just means the result-copy
  line never runs for that element) -- confirmed the exact same
  "one bad element never aborts the whole call" rule the destructed
  case uses, not a separate path.
- A string element resolves via `find_object()` the same way the scalar
  form already does, skipping (not erroring) on a miss. The same args
  are pushed for every element (read from one saved stack position each
  iteration, not a per-element slice).

**Real corpus cross-check, confirming this is genuinely needed, not a
speculative generalization:** `array_var->method(args)` is real and
widespread, not narrow to AetherMUD -- confirmed live across every
vendored corpus this project tracks (`dead-souls`, `lima`,
`es2_mudlib`, `nightmare3`, this project's own bundled `mudlib/`
corpus, and AetherMUD's own `reinitiate()` itself). One real usage
confirms the *result-array* semantics specifically matter, not just
dispatch: `lima/lib/secure/simul_efun/userfuncs.c`'s own
`users()->query_body() - ({0})` explicitly filters literal-0 non-hit
entries back out -- live proof real mudlib code relies on exactly the
"unfilled slot is int 0" fallback found in the real source, not some
other sentinel. No real corpus evidence of the *combined*
array-of-targets-plus-array-of-(funcname,boundargs) form anywhere, and
this driver's scalar-target `call_other` never supported an
array-form function name either -- that combination stayed out of
scope, matching this project's own bounded-to-real-evidence discipline.

**Built:** the array-form branch added to `call_other`'s own registered
efun body (`EfunTable.cpp`), checked before the existing scalar
branches. Reuses `VM::callFunction()` directly as the per-element apply
mechanism -- it already implements the destructed-target and
missing-function "return a default `Value{}`, never throw" behavior
real `call_all_other()` needs, for free, confirmed against that
method's own header comment. The new code resolves each element
(`shared_ptr<LpcObject>` used directly, a string resolved via
`vm.findObject()`, anything else left null), calls through
`VM::callFunction()` when a target was resolved, and collapses the
result into the output array -- converting this driver's own
`monostate` (whether from a skip or a genuinely void function falling
through, both observably identical to a real int 0 on real FluffOS
too) into a real `int64_t{0}`, matching real `allocate_array()`'s own
plain `const0` default exactly.

**5 new regression tests (761 total, up from 756):** array-form
call_other calling every element and returning results in the right
order; a destructed element silently skipped, its own slot left at 0,
the rest of the array still called; a missing function on one element
likewise skipped, the rest still called; a string element resolved via
`find_object()` to the real blueprint object; and the exact real
`reinitiate()` shape itself (`obs->move(dest)`, return value discarded,
only each element's own real environment change checked afterward)
confirming the side effect happens for every element, not just the
first. The original 756 re-run unchanged.

**Live-verified against the real running driver and the real AetherMUD
mudlib specifically, before and after:** before, walking a fresh
character through the same Chi-Town rooms the prior session's broader
pass already exercised reliably reproduced the real `call_other: first
argument must be an object or a string path` error in the driver's own
log every time; after, zero such errors across the same walk. Went
further than "no error," per this row's own instruction to confirm
`reinitiate()`'s real intended behavior: a temporary debug build of
`std/room/exits.c` (reverted afterward, confirmed byte-identical via
`diff`) traced every real `reinitiate()` firing during that same walk
-- `/domains/Praxis/setter` (1 object), `/domains/ChiTown/areas/
chitown_start` (2), `/domains/ChiTown/areas/chitown_gate` (5),
`/domains/ChiTown/areas/chitown_burbs` (1) -- each one moving every
object out to `ROOM_VOID` and back, the result array's own `sizeof()`
matching the input count every time, and the room's own final
inventory count matching what it started with, confirming the real
"kick everyone out and back in" refresh genuinely completes correctly,
not just avoids erroring. Test-account/postal state created during this
verification deleted afterward, matching this row's own established
cleanup precedent. Full real-semantics citation trail, the fix, and
every regression test are in ROADMAP.md row 3.9 itself now, not just
here.

**2026-08-25 (a further session, same day): row 3.9's open sprintf trace
finished. Real root cause found and fixed (a driver-side gap, confirmed
against real FluffOS source, not the mudlib's own bug), 5 new
regression tests (756 total, up from 751), live-verified against the
real driver and the real AetherMUD Rifts mudlib before and after.
Followed by a broader content pass through that same mudlib: one
further real driver gap found (not fixed this session), two real
mudlib-content issues (not this driver's), reported below.**

**The finished trace.** The prior session's own static-only read had
already ruled out `sprintf()`'s own `%d` strictness and this driver's
`(int)` cast semantics (both confirmed byte-for-byte faithful to real
FluffOS by direct citation) and traced `query_next_level_xp()`'s two
branches (`ADVANCE_D->get_exp()`, an OCC's own `xp_table`) as far as
static reading allowed, finding every literal genuinely int-typed but
never confirming the actual runtime value. This session picked it up
empirically instead of continuing to read: instrumented
`query_next_level_xp()` directly with real `typeof()` tracing (a
temporary debug build of `cmds/mortal/_score.c`, reverted afterward,
confirmed byte-identical via `diff`) and drove a real fresh character
through this driver's own `Scheduler`/`VM` end to end via a real TCP
session. Every value along both of the prior session's own suspect
paths came back genuinely `int` -- closing off `query_next_level_xp()`'s
own return value as the culprit entirely, the concrete thing the prior
session's own budget could not settle. The actual non-int value turned
out to be `show_experience()`'s *other* local: `exp = (int)who->
query_exp()`, where `std/living.c`'s own `query_exp()` returns
`player_data["general"]["experience"]` -- a genuinely missing,
non-column mapping key on a freshly created character whose experience
has never been written.

**Root cause, confirmed against real FluffOS source directly, not
inferred:** real `mapping.c`'s own `find_in_mapping()` returns
`&const0u` on a genuine miss; real `main.c`'s own `const0u.subtype =
T_UNDEFINED;` confirms `T_UNDEFINED` (`lpc.h`: `#define T_UNDEFINED
0x4`) is a *subtype* flag on an ordinary `T_NUMBER` svalue, not a
distinct top-level type. A missing-mapping-key value is therefore
genuine `T_NUMBER` at the real C level and passes real `sprintf()`'s own
`%d`/`%o`/`%x`/`%c` type check (`sprintf.c:1180`: `carg->type !=
T_NUMBER`) without issue, printing as plain `0`. This driver's own
`OpCode::Index` (`VM.cpp`) already correctly returns its own
`monostate` (this driver's T_UNDEFINED-equivalent) for a missing,
non-column `map[key]`, and that value already participates correctly in
arithmetic (`asArithmeticOperand()`) and string-concatenation
(`formatNumberForConcat()`) exactly like a real 0 -- both pre-existing,
already-documented, correct. The actual gap was narrower: `sprintf()`'s
own `%d`/`%o`/`%x`/`%c` argument-type checks (`EfunTable.cpp`) rejected
`monostate` outright, the one numeric context in this driver that had
not been given the same exemption every other one already had.

**The fix.** One new helper, `sprintfNumericArg()` (a local lambda
inside the same block `sprintfImpl` lives in, captured by it): accepts
`int64_t` unchanged, treats `monostate` as `0`, used at all four
affected call sites. `%s`/`%O` untouched (`%O` already accepted every
kind unconditionally; `%s` correctly still requires a real string). The
`'*'`/`'.*'` dynamic field-width argument checks were deliberately left
strict-`int64_t`-only -- zero real evidence anywhere in this mudlib or
any other vendored corpus of a missing-mapping-key value ever being
used as a field width, matching this project's own bounded-to-
real-evidence discipline (row 2.12's own precedent).

**5 new regression tests (756 total, up from 751):** `%d` still throws
on a genuinely non-numeric argument (a string, mirroring the
pre-existing `%c` symmetric test); `%d` on a missing mapping key prints
`0`; `%o`/`%x` on a missing mapping key both print `0`; `%c` on a
missing mapping key emits a real NUL byte; and the exact real-world
shape this trace found -- a nested mapping lookup where the outer key
exists but the inner one does not, cast through a no-op `(int)` into a
multi-argument `%d` format string matching `show_experience()`'s own
real one -- reproducing `Level: 1    XP: 0 / 2000 (next level)` exactly.
The original 751 re-run unchanged (same names, same count) to confirm
the fix is additive.

**Live-verified against the real running driver** (a new
`etc/driver_aethermud.cfg` in this main checkout, values taken directly
from the mudlib's own bundled `secure/cfg/mudos.cfg`, kept in the repo
as a real, reusable boot config) and the real AetherMUD mudlib
specifically, before and after the fix: before, the score panel
silently stopped mid-render at the `EXPERIENCE` header row (driver log
showing the real `sprintf: %d argument is not an int` failure, caught
by `finish_creation()`'s own `catch()` for the automatic end-of-chargen
call, uncaught -- `[net] ... input handling failed` -- for an explicit
`score` command); after, a full, correct score panel renders every
time, `Level: 1    XP: 0 / 2000 (next level)` included, zero errors of
any kind across several fresh characters.

**Broader pass through this Rifts-lineage mudlib's own content, same
live session, real gaps found and categorized (not exhaustive, per
instruction not to force full coverage in one sitting):**

- `inventory`/`equipment` real and correct per OCC. One real
  mudlib-content bug (not this driver's): `equipment`'s own display
  prints "Right hand: ... (wielded in left hand)", a hand-label
  mismatch in the mudlib's own formatting code.
- `skills` lists real, per-OCC granted skills correctly (matching
  Vagabond's own `base_skills`/`occ_skills` from `daemon/occ.c`). One
  real gap investigated and categorized as mudlib-content, not this
  driver's: `%^BOLD%^`/`%^GREEN%^`/`%^RESET%^` colour tags print
  raw/untranslated even after `colorize`. Traced before categorizing:
  this mudlib's own colour translation lives entirely inside
  `std/user.c`'s own `receive_message()`, reached only via message-
  class-aware output -- `_score.c`/`_skills.c` instead call the raw
  `write()` efun directly with embedded `%^` tags, and this mudlib's
  own `catch_tell(str) { receive(str); }` is a bare passthrough with no
  colour handling at all. Confirmed against real `simulate.c`'s own
  `do_write()` -> `print_svalue()` -> `tell_object()` -> (real FluffOS,
  `INTERACTIVE_CATCH_TELL`) `catch_tell()`: the identical real call
  chain, meaning real FluffOS would show the same raw tags for this
  exact mudlib code -- a real mudlib-content inconsistency, not a
  driver gap.
- **A real driver-side gap found, not fixed this session** (found late
  in this row's own time budget, matching the "diagnose with rigor, do
  not force a fix" precedent from the original trace): `std/room/
  exits.c`'s own `reinitiate()` does `obs->move(ROOM_VOID);
  obs->move(this_object());` where `obs` is `object *` (an array) --
  real FluffOS's own `call_other()` signature (`func_spec.c`: `unknown
  call_other(object | string | object *, string | mixed *, ...)`)
  documents `object *` as a real, first-class accepted form (a
  distributed call across every array element), confirmed directly.
  This driver's own arrow-call dispatch instead throws "call_other:
  first argument must be an object or a string path" for an array
  target, surfacing repeatedly in the driver's own log every time a
  room with inventory resets. Not yet scoped into its own ROADMAP.md
  row -- a real candidate for a future dedicated session.
- Combat: `kill`/`attack` exist and respond (a real newbie-safe-zone
  gate blocked the one attempt made), but no live fight was actually
  reached within this session's own exploration budget -- not ruled in
  or out.
- Zone/race/OCC/alignment chargen (the real ~60-entry Rifts race list,
  OCC-eligibility branching, all seven real Palladium alignments, the
  full OCC list) all confirmed real and correctly gated.

Test-account/character/postal state created during this session's own
live verification deleted afterward (by file timestamp against this
session's own newly-created `etc/driver_aethermud.cfg`, not `git
status`, matching row 3.9's own established reasoning for why -- see
that row's own 2026-08-21 note), matching row 2.9/2.12/2.15's own
cleanup precedent. Full trace, root cause, fix, and every categorized
finding are in ROADMAP.md row 3.9 itself now, not just here.

**2026-08-25 (a further session, same day): row 2.5's v1 first slice
built, exactly the design from the scoping session immediately below
plus two real corrections found while wiring it together. 4 new
regression tests, 751 total (751 = 747 + 4, confirmed by rebuilding and
running `build/test/amlp_tests` directly), all passing; the original
747 re-verified unchanged (same names, same count) to confirm the new
async path is genuinely additive, not just re-trusted from before this
session.**

Built, in the order ROADMAP.md row 2.5's own 5-item first slice lists
them: `include/amlp/scheduler/Task.hpp` (a real, minimal `Task<Value>`
coroutine type, header-only, deliberately independent of Scheduler's
own class); `FunctionEntry::isAsync` and `OpCode::Suspend`
(`Bytecode.hpp`); `VM::runAsync()` (a genuinely separate coroutine next
to the unchanged `run()`, implementing a deliberately small opcode
subset -- `PushInt`/`PushLocal`/`StoreLocal`/`Add`(int-only)/`Call`/
`Suspend`/`Return`/`Halt` -- covering exactly what this row's own
hand-built test bytecode needs, throwing `NotImplementedError` for
anything else rather than misbehaving silently, real row 2.6 grammar
not existing yet to need more); and `Scheduler::run()` draining a new
queue each tick.

**Two real corrections found while wiring the pieces together, neither
visible from the docs-only scoping pass, both the same "found before
finishing, not before starting" discipline every prior Phase 2 build
session has already used:**

1. **Circular-dependency correction.** The design as scoped put the
   timer-park queue and `resumeReadyTasks()` on `Scheduler`. Confirmed
   directly before writing that code: `src/scheduler/CMakeLists.txt`
   links `scheduler` against `vm`, never the reverse, and `VM.cpp` has
   zero existing call sites referencing `Scheduler`'s own concrete
   class at all -- every real `call_out()`/`heart_beat()` efun bridges
   the two from `EfunTable.cpp` instead, one layer up, a real,
   pre-existing precedent, not invented for this row. Giving `VM.cpp` a
   new call into `Scheduler.hpp` would have created a genuine circular
   library dependency. Fixed by flipping ownership: the parked-handle
   queue and its own per-callback isolation now live on `VM`
   (`pendingAsyncResumes_`, `resumeReadyAsyncTasks()`, `suspendFor()`),
   and `Scheduler::run()` calls `vm_.resumeReadyAsyncTasks(now)` each
   tick -- mirroring `processPendingReplacePrograms()`'s own already-
   established "VM owns the pending queue, Scheduler drains it every
   tick" shape immediately next to it in that same loop, not a new
   pattern invented for this row.
2. **A real C++20 gotcha, not an architecture issue.** `Task<T>::
   promise_type` had no user-declared constructor, making it a plain
   aggregate; C++20's parenthesized aggregate-init (P0960) let the
   compiler try constructing it positionally from `runAsync()`'s own
   argument list (preceded by its implicit `this`, since it is a
   non-static member coroutine) when deciding how to build the promise
   object -- silently landing the `VM&` receiver into the promise's
   first member (`value`, a `Value`) and failing to compile with a
   confusing "no matching `Value` constructor" error nowhere near
   `Task.hpp`. Fixed with one explicit `promise_type() = default;`,
   restoring ordinary default construction.

**Deliberately not given `run()`'s own `ObjectFrameGuard`/`OriginGuard`
treatment:** `runAsync()` never pushes `obj` onto `VM::callStack_`/
`objectChangeStack_`/`originStack_`, all three single VM-wide vectors
shared with every ordinary synchronous call still happening elsewhere
while a task is parked -- doing so would risk real, silent shared-stack
corruption across a suspend/resume boundary. `currentObject()`/
`origin()` are therefore not reliable from inside a running-or-parked
async task in this v1 -- a real, named gap, not an oversight, left for
row 2.6. `evalCost_` is shared unchanged, deliberately, matching the
hard requirement the scoping session already named.

**The 4 new regression tests:** a minimal hand-built async function
that suspends on `Suspend` and resumes with its own local state
(`x = 42`) intact; a direct check that an ordinary, really-compiled (not
hand-built) synchronous function run through the unchanged
`callFunction()`/`run()` path is bit-for-bit unaffected by any of this
row's new code existing in the same binary; the specific scenario the
scoping session found the old `TaskFrame`-sketch would have broken on --
an `async` function `a()` calling another `async` function `b()`
through a perfectly ordinary, unmarked `OpCode::Call`, where `b()` (not
`a()`) is the one that actually suspends, proving the suspension still
correctly propagates all the way up through `a()`'s own `co_await`
chain to the real driver (result `1107`); and a parked async task
coexisting correctly with a real ordinary `call_out` across the same
tick sequence (`scheduler.tickCallOuts()` then
`vm.resumeReadyAsyncTasks()`, the same order `Scheduler::run()` itself
uses) -- the closest honest proof available of real-mechanism
integration given no LPC source can reach `OpCode::Suspend` yet.

**Live-verified against the real running driver** (`./build/amlp
etc/driver.cfg`) and the real bundled mudlib over a real TCP session,
since no LPC source can trigger the new async path itself yet: fresh
account/character creation, reconnect, full movement loop (gatehouse ->
watch room -> gatehouse), `talk mabb`/`talk mabb about rod` (both
produce real dialogue once actually standing in the watch room -- Old
Mabb is not in the starting gatehouse, confirmed correct only after
moving there), and the reeve's rod's `create`/`purge` mechanic, all
working exactly as before this row, with zero errors or crashes in the
driver's own log across two full sessions (the only log lines produced,
"[object] source file not found: mudlib/command/look.c" and similarly
for `talk`/`inventory`, are the pre-existing, harmless virtual-command
compile-miss fallback these `add_action`-driven verbs already logged
before this row, confirmed by reading the driver's own startup/dispatch
code, not a new regression). Test-account/character/created-object
state (`livecheck2026`, `testhero.o`, `test_widget.c`) deleted
afterward, matching row 2.9/2.12/2.15's own established cleanup
precedent.

**Explicitly still open, unchanged from the scoping session's own
deferral list:** 2.6's real grammar/codegen (now genuinely unblocked --
this row's own machinery is real and tested), 2.7's `call_out_future()`,
2.8's Hydra parallelism, 2.18's async I/O efuns. One additional real gap
surfaced by the build itself, not previously named: a `Task<T>` whose
coroutine is destroyed while still parked leaves a dangling
`coroutine_handle` in `VM::pendingAsyncResumes_` -- real cancellation
does not exist yet, and nothing in this row's own scope creates and
abandons a `Task` early, so this is named in `Task.hpp` rather than
fixed here. Full detail, including the exact opcode list and file
layout, is in ROADMAP.md row 2.5 itself now, not just here.

**2026-08-25: Tier 2 cold-start scoping session, row 2.5 (C++20 coroutine
scheduler) picked over 2.11 (LLVM JIT) and 3.3 (generational GC), the
three remaining unscoped-for-real large Phase 2/3 items. Docs-only
session per instruction -- no implementation code written, ROADMAP.md
row 2.5's own note replaced with a concrete first-slice design. 747
tests confirmed passing by rebuilding and running `build/test/amlp_tests`
directly (not trusted from the prior session's own count), unchanged --
expected, since no driver code was touched this session.**

**Why 2.5 over the other two, weighed on real evidence gathered fresh
this session, not repeated from 2026-08-21:**

- **2.11 (JIT):** re-confirmed directly against `CMakeLists.txt` and
  every `src/*/CMakeLists.txt`, this driver's entire real dependency
  footprint today is still `pkg_check_modules(PCRE2 REQUIRED libpcre2-8)`
  + `pkg_check_modules(SQLITE3 REQUIRED sqlite3)` (both in
  `src/efun/CMakeLists.txt`) + a direct `crypt` link (libxcrypt) -- zero
  `LLVM` hits anywhere in the repo outside prose. `src/jit/` itself holds
  only `.gitkeep` and `instruct.md`; it is not even wired into the root
  `CMakeLists.txt`'s own `add_subdirectory()` list. LLVM 17+ dev
  libraries would be by far the largest single toolchain jump this
  project has ever taken, for a row with zero real corpus evidence behind
  it (a pure performance feature; no vendored mudlib "needs" a JIT the
  way it needs a specific efun) and a soft precondition (row 1.8, "bit-
  identical output for every test case," still open) this project has
  not actually cleared. Scoping it deeply this session would not have
  reduced its real risk, since that risk is a toolchain/strategic
  buy-in decision, not a design-clarity gap a docs-only session can
  resolve.
- **3.3 (GC):** the shared_ptr migration surface, re-grepped directly
  this session (`shared_ptr<LpcObject>`/`Array`/`Mapping`/`Closure`
  across `src/`+`include/`), is 556 real hits today -- up from 546 two
  sessions ago, up from 525 before that. The growth trend flagged in the
  last two sessions is confirmed again, not just repeated. But `src/gc/
  instruct.md` itself is explicit that this row must not start "until
  all Phase 0, 1, and 2 work is complete" -- Phase 2 is 5/22 today (`2.1`,
  `2.4`, `2.9`, `2.12`, `2.15`, counted directly off ROADMAP.md's own
  checkboxes this session), nowhere near done. A first-slice plan written
  now would face the same fate as the count itself: stale before it can
  ever be acted on, since every session that touches `src/`/`include/`
  between now and Phase 2's actual completion adds more `shared_ptr`
  sites the plan did not account for. Scoping 3.3 deeply is better spent
  closer to when Phase 2 actually closes.
- **2.5 (coroutine scheduler):** unlike the other two, this row is not
  gated behind unrelated work finishing first, and -- like 2.1 before it
  -- it gates real, named follow-on rows of its own (2.6's `async`/
  `await` grammar, 2.7's `call_out_future()`, 2.8's Hydra parallel tasks,
  2.18's async `http_get`/`http_post`, all explicitly blocked on it in
  their own ROADMAP.md cells). Its real risk is squarely a design-clarity
  problem, not a toolchain or sequencing one: `VM::run()` is 2,768 real
  lines exercised by all 747 tests, the single highest-blast-radius file
  in the whole driver, and this row's own `instruct.md` sketch turned out
  (see below) to be incomplete for exactly the case that matters most. A
  docs-only cold-start session is the correct tool for that kind of risk
  -- settle the design on paper before any code touches that file, which
  is exactly what this session did.

**Real scope findings, from reading the actual architecture, not the
abstract feature:** `VM::run()` is one deeply recursive C++ function --
every nested LPC call (`OpCode::Call`/`CallOther`/`CallEfun`/simul_efun,
5 real call sites in `VM.cpp`, confirmed by direct grep) is a plain
synchronous `Value result = run(...)` call growing the real C++ stack,
with `locals`/`localStack`/`catchFrames`/`ip` living as ordinary C++
automatic storage in that one activation. `src/scheduler/instruct.md`'s
own existing 2.6 sketch (manual `TaskFrame` capture-and-resume: serialize
the current frame on `await`, return a sentinel, restore into a fresh
`run()` call later) only actually covers a single frame suspending
directly in a task's own top-level function body. Read literally, it
does not handle `await` reached through an intervening plain LPC call --
the sentinel would need to be manually detected and re-propagated by
hand at every one of the 5 call sites, correctly, for arbitrary nesting
depth, with `catchFrames` still live across the suspend point. That is a
large, error-prone hand-reimplementation of what a real C++20 stackless
coroutine's compiler-generated state machine already does for free via
`co_await` chaining -- and this driver has zero real coroutine usage
today (`co_await`/`co_yield`/`co_return`/`<coroutine>`, direct grep,
zero hits outside this row's own `instruct.md`), so nothing forced the
manual-frame shortcut except that it was never checked against the
nested-call case.

**Concrete first-slice proposal (written into ROADMAP.md row 2.5, not
left in chat only):** a real `Task<Value>` coroutine type (a minimal
hand-rolled promise_type over the standard `<coroutine>` header, no new
external dependency), a timer awaitable feeding `Scheduler`'s own
already-correct `suspend(Task&, duration)` priority-queue design
(unchanged from the original sketch -- only the frame-capture half
needed revision), a new `resumeReadyTasks(now)` step in `Scheduler::run()`
reusing the existing per-callback try/catch isolation `tickCallOuts()`/
`tickHeartbeats()` already have, and a new parallel `runAsync()` entry
point in `VM` used only for functions flagged `async` (the flag itself
is 2.6's job) via `co_await` at `OpCode::Call`'s dispatch site plus a new
`OpCode::Suspend` -- leaving the existing synchronous `run()` completely
untouched for every ordinary call, protecting all 747 tests' behavior
and performance by construction. Explicitly deferred out of this slice,
each independently bounded: 2.6's real grammar/codegen, 2.7's
`call_out_future()`, 2.8's Hydra `std::jthread` speculative parallelism
(materially larger and riskier, must not be conflated with the base
slice), and 2.18's async I/O efuns (no I/O-driven awaitable exists yet
to build on). Full reasoning, including the exact call-site count and
the `evalCost_`-sharing invariant, is in ROADMAP.md row 2.5 itself now,
not just here.

**2026-08-23 (a further session, same day): bundled test mudlib given a
real setting, replacing the earlier session's own "test entrance
hall"/"chamber A/B/C" scaffolding, at the user's explicit request.**
Mudlib content only, no driver code changed except one regression test's
own expected string (see below).

World: a half-collapsed old kingdom, low-magic and grounded. Stonewick,
a settlement gutted thirty-odd years back by the Long Burn -- a real,
mundane dry-summer granary fire, not a curse or a prophecy -- after
which the garrison never recovered its numbers and the town was mostly
abandoned to scavengers, deserters, and whatever has since moved into
the flooded cellars and rotten granary lofts. The starting area is the
Ashgate, the old east-wall gatehouse (`/single/gatehouse.c`, replacing
`start_room.c`), not a bare "entrance hall." The same 2x2 exit topology
from the previous session is kept exactly (it already worked and was
tested), just renamed and re-described: the watch room
(`/single/watch_room.c`, was `room_chamber_a.c`), the sunken court
(`/single/sunken_court.c`, was `room_chamber_b.c`, a flooded former
market square), and the granary loft (`/single/granary_loft.c`, was
`room_chamber_c.c`, where Mabb's own dialogue warns something has grown
in the grain-rot).

New NPC: Old Mabb (`/clone/old_mabb.c`), a scavenger squatting in the
watch room, placed there once at that room's own `create()` time (not
re-cloned per visitor). Real dialogue via a genuine `add_action`-driven
`talk <name> [about <topic>]` command, not a static description block:
a bare greeting, plus real per-topic answers about the tower, the rod,
the granary's rot, and the Long Burn itself (this last one explicit
about the fire being ordinary bad luck, "wasn't magic, wasn't a curse"
-- matching the user's own explicit "keep the catastrophe mundane, not
high fantasy prophecy stuff" instruction), plus an honest fallback for
anything else.

The wand of creation gained a real in-fiction identity: the reeve's
rod, the old garrison stores-reeve's requisition tool, left untouched on
the gatehouse's own requisition desk since the reeve died or fled with
everyone else, picked up by a new arrival the same way the previous
session's wand hand-out already worked mechanically. The underlying file
deliberately keeps its old name and path, `/clone/wand_of_creation.c`
(new constant `REEVES_ROD_OB` in `globals.h` for other files to refer to
it by) -- real regression tests in `test/test_lexer.cpp` read this exact
file off disk by path, and a player never sees a filename, so renaming
it on disk would have been pure risk for no in-fiction benefit. The
`clone`/`purge`/`create` command verbs themselves are unchanged (no good
specific reason to rename what a player types), and so is almost every
message they print -- only the held()-denial message actually named the
old item ("You are not holding the wand of creation.") and was changed
("You are not holding the reeve's rod."), with the one real regression
test asserting that exact string updated in the same lockstep, not left
to drift or worked around by burying the old name in unrelated text.
`short()`/`long()`/`id()` were rewritten in full.

Login banner/`/etc/motd`/`help.c` rewritten to match: no more "Welcome
to Library," a plain framing of Stonewick and the gatehouse, the real
runnable command list including the new `talk` command, kept in sync
across all three files the same way the previous session's rewrite
already established.

1 regression test string updated (`testWandOfCreationHeldGuardBlocksAllCommandsWhenOnlyColocatedNotHeld`,
`test/test_lexer.cpp`) to match the reflavored denial message; no new
regression tests added (mudlib content, not driver code -- this row's
own verification is the live session below, matching the previous
mudlib-rewrite session's own precedent). 747 tests passing, unchanged
from the prior session, zero regressions.

Live-verified over a real TCP session against the real running driver
and the rewritten mudlib end to end: fresh account/character creation;
the full four-room loop (gatehouse -> watch room -> granary loft ->
sunken court -> gatehouse) with correct new room names/descriptions at
every stop; all five real `talk mabb about <topic>` topics plus the bare
greeting and the unknown-topic fallback, all producing their own real,
distinct text; `clone`/`create`/`purge` all still working under the
reeve's rod identity (`Cloned into your inventory: /command/help`,
`Created and placed here: a rusty tin cup`, `Purged: a rusty tin cup`);
and character persistence unbroken by the rename, `quit` then a fresh
reconnect as the same account correctly reporting `login_count` 1 then
2. Test account/character files and the one generated `/data/created/`
file from the live `create` test deleted afterward.

**2026-08-23 (a later session, same day): rows 2.2/2.3/2.4 re-scoped
against the real shipped row 2.1 code, not the pre-2.1 abstract plan;
row 2.4 built.** Re-read `StateSerializer.cpp`/`EfunTable.cpp` directly
for each of the three rows the earlier 2026-08-21 scoping session had
gated on 2.1 landing, per this session's own instruction to confirm real
scope now that it has.

Row 2.4 (dual persistence) confirmed genuinely small, as the original
note predicted, and built: `dump_state()`/`restore_state()` and
`save_object()`/`restore_object()` share zero code path (`dump_state()`
never reads a save file; `restoreState()` reconstructs via
`cloneObject()`/`loadObject()` directly, never calling `restore_object()`
itself), so there is no real conflict to design around, only a real
confirmation and test worth having. `ObjectManager::retainRestoredObjects()`
(row 2.1's own lifetime fix) turned out load-bearing for this row's own
test to even be meaningful. 1 new regression test (747 total, up from
746). Live-verified over two real TCP sessions on the same real running
driver/bundled mudlib: a real account/character's `save_object()`-backed
`login_count` correctly went 1 to 2 across an intervening `dump_state()`
world snapshot, proven through the real `login.c` login flow, not a
synthetic C++-only check. See ROADMAP.md's own row 2.4 entry for the
full detail.

Row 2.2 (object swapout) and row 2.3 (hotboot) were **not** picked up
this session -- both turned out to need real, undone design work even
with 2.1 now real, not the smaller "depends on 2.1's format" framing
the pre-2.1 notes carried. Row 2.2: `StateSerializer`'s own id-table is
built in one coherent whole-world pass and always reconstructs every
referenced object fresh; it has no way to say "this id refers to an
object already live in memory elsewhere, resolve to that instance, do
not reconstruct it" -- exactly what swapping one idle object in and out
while the rest of the live world stays resident needs. 2.1 clarifies
that this is a *harder* reference-identity problem than the whole-world
case, not a smaller one. Row 2.3: the real `fork()`/`exec()`/
`SCM_RIGHTS` systems work is completely untouched by 2.1 (2.1 has no
awareness of network fds at all), and hotboot additionally needs a new
correlation layer 2.1 does not provide -- mapping each inherited,
still-open connection fd to the correct restored player object, since
`StateSerializer`'s dump-scoped ids are not by themselves a stable
enough handle for that across a fresh enumeration. `retainRestoredObjects()`
is a real, already-solved prerequisite for this row specifically (without
it the whole restored world would evaporate before a single reconnected
fd could be served), worth recording since it was fixed as a side effect
of landing 2.1, not deliberately built for hotboot. Both rows' own
ROADMAP.md entries were rewritten in place with these concrete,
post-2.1 findings, replacing the pre-2.1 text rather than leaving it to
be mistaken for still-current.

747 tests passing (up from 746), zero regressions.

**2026-08-23: row 2.1 (world statedump) v1 first slice built, exactly the
2026-08-22 scoping session's own design, no more.** New `StateSerializer`
(`src/persist`), an id-table, two-pass whole-world dump/restore extending
`EfunTable.cpp`'s existing `serializeValue()`/`deserializeValue()` tag
scheme with `O<id>` (object reference) and `C` (closure) instead of
replacing it. New `dump_state(string)`/`restore_state(string)` efuns,
gated the same `checkValidPath()` way every other file efun already is.
One real gap found and fixed while building this, not in the original
design note: a freshly restored object with nothing yet referencing it
was silently freed the instant `restoreState()` returned (`cloneObject()`
has never added a clone to any persistent registry) -- fixed with a new
`ObjectManager::retainRestoredObjects()`, erased again on an explicit
later `destruct()` so it does not pin memory forever. 3 new regression
tests (746 total, up from 743), including the row's own core reference-
identity guarantee tested directly (a room, two inventory items, one
holding a plain object-typed variable pointing back at the room, dumped
and restored into a completely separate `ObjectManager`/`VM`). Live-
verified across two genuinely separate driver processes over two real
TCP sessions: process 1's real login flow, wand hand-out, and
`dump_state()`; process 1 killed outright; a fresh process 2's
`restore_state()` and a follow-up `eval` walking `objects()`/
`environment()`/`all_inventory()` confirming the real two-level
environment/inventory chain (wand in restored player in room)
reconstructed correctly. Still open, as designed: call_outs/heartbeats,
actions/shadow/snoop, parse-info, reset/cleanup timing. See ROADMAP.md's
own row 2.1 "v1 built" note for the full detail.

Also this session, separately (mudlib content, not driver code): the
bundled test mudlib's login banner and `/etc/motd` were rewritten at the
user's explicit request -- the old "Welcome to Library! ... only
commands in this little mudlib are: dest, update, ed, eval, efun, rm..."
text advertised several commands (`dest`/`update`/`ed`/`efun`/`rm`) that
were never actually wired to anything real in this pared-down mudlib
(only `eval`/`quit`/`say`/`shutdown`/`who` under `mudlib/command/` are
real), and named the mud by name in a way the user asked removed
entirely, not just softened. Replaced with a plain "AMLP LPC driver --
test session" banner and a `motd`/new `help` command (kept in sync)
listing only the commands that actually work. Also added real,
navigable multi-room movement, which did not exist before this session
at all (the mudlib had exactly one room and no way to leave it): a new
`/inherit/room.c` mixin (`set_exits()`/`do_go()`/`room::init()`) and
three new static rooms (`/single/room_chamber_{a,b,c}.c`) forming a 2x2
loop with the existing entrance hall (`/single/start_room.c`, updated to
inherit the same mixin) -- `north`/`south`/`east`/`west`, each room's own
`init()` printing its description on entry (no `look` command exists in
this mudlib) and registering its own real exits. Live-verified over a
real TCP session: the full four-room loop (north, east, south, west)
returns to the entrance hall with the correct description at each stop,
and the new `help` command prints the real command list. Test account/
character files created during both live-verification passes deleted
afterward.

**2026-08-22 (a later session, same day): fresh full-project status
sweep, requested after row 1.7's own remaining `call_out_info()`/
`input_to()` privilege-gate follow-on landed. Docs-only session --
`ROADMAP.md`/`STATUS.md`/`COMPARISON.md` only, no implementation code
written.**

**Real, measured numbers, re-derived from source this session, not
carried forward from any prior note:**

| Phase | Rows | Done | % done |
|---|---|---|---|
| 0, Stabilize | 16 | 16 | 100% |
| 1, Dialect universality (real blockers only, 5 DGD-only rows excluded) | 11 | 10 | 91% |
| 2, Beyond both (novel features) | 22 | 3 | 14% |
| 3, Production hardening + docs | 9 | 1 | 11% |

Phase 0: unchanged, 16/16, re-confirmed by direct `awk`-count over
`ROADMAP.md`'s own Phase 0 section rather than trusted forward.

Phase 1: also unchanged at 10/11 real-blocker rows (row 1.7 was already
checked off before this session; what closed this session was one of
its own remaining sub-items, `call_out_info()`/`input_to()`'s
`privilege_violation()` gates, not the row's own checkbox). The prompt
that opened this session asked for the row's own scope note to be
re-confirmed as "essentially exhausted of real, well-evidenced items" --
re-read the row 1.7 cell in full rather than trusting that framing on
its face, and it holds up: what remains is `H_LOAD_UIDS`/`H_CLONE_UIDS`/
`H_INCLUDE_DIRS` dispatch (3 real call sites total, all in one file,
`core-lib/secure/master/hooks.c`, plus a handful more inside LDMud's own
bundled driver test fixtures, not independent gameplay corpora -- weak,
single-source evidence, not the multi-corpus bar this project's own
`parse_*`/two-object-rule work cleared), real per-hook type-map
validation (no corpus signal either way, a structural completeness item
not an observed gap), plain dialect-agnostic `lambda()` (zero real
corpus hits, confirmed fresh by an earlier session and not re-disputed
here), and `inaugurate_master()`'s own arg=1/2/3 master-reload/
reactivation cases (a real trigger point this driver has never wired,
but no session has yet corpus-checked it specifically -- flagged here as
an actual gap in the evidence record, not assumed zero just because
everything else on this row turned out to be). Row 1.8
(`#'lfun::`/`#'sefun::`/`#'var::`) stays the one fully open row, zero
real corpus hits confirmed by an earlier session, not re-disputed here
either. Net: Phase 1's real-blocker fraction has not moved, but its own
remaining surface really is now almost entirely zero-or-near-zero-
evidence work, matching the prompt's own framing.

**Phase 2 and Phase 3 are not "still 0%, planning docs only."** This is
the one place this session's own findings genuinely correct the
premise it started from, not just confirm it. Direct evidence: `git
log` shows three Phase 2 rows already built and committed in an earlier
session on 2026-08-21 -- 2.9 (apply cache), 2.12 (full PCRE
`pcre_match`/`pcre_assoc` suite), 2.15 (SQLite `db_*` efuns) -- plus one
Phase 3 row, 3.9 (a real third-party mudlib, "AetherMUD", confirmed
booting and playable against this driver). `ROADMAP.md`'s own Phase 2
section already carried a full "cold-start scoping session, 2026-08-21"
pass across every remaining row too (the same session that later built
2.9/2.12/2.15), and this project's own prior session (also dated
2026-08-22, immediately below this entry) had already picked row 2.1
(world-level statedump) for a dedicated deeper scoping pass and written
a concrete first-slice design directly into that row's own cell. None
of this matches "zero implemented code, planning documents only" --
`COMPARISON.md` had exactly that stale framing in three separate places
(its own summary paragraph, its "Phase 2 and Phase 3: not started"
section, and its "every Phase 2/3 differentiator is a plan, not code"
bullet), left over from its own 2026-08-21 sweep, which itself predated
the 2.9/2.12/2.15 landings by hours on the same day and was never
re-swept after. All three corrected in this session (see `COMPARISON.md`'s
own new "Re-swept 2026-08-22" dated note and the sections it points at).

**Test count discrepancy, found and worth naming rather than quietly
smoothing over:** this session's own opening prompt cited "715 tests
passing," matching `ROADMAP.md` row 1.7's own dated note ("715 tests
passing (up from 709)"). Rebuilding and running the real test binary
directly this session (not trusted from any note) gives **743**, not
715. Root cause, confirmed by reading `git log` timestamps directly:
row 1.7's own "(709 to 715)" commit (`91f1a58`, 2026-08-22 14:32) landed
*after* 2.9/2.12/2.15's own commit (`a5690fb`, 2026-08-21 23:30), which
had already pushed the real suite total well past 715 by the time row
1.7's own slice ran. Row 1.7's own commit message measured only its own
local before/after delta (709 to 715) without re-checking the actual
interim whole-suite total, so its own "715" was already stale relative
to the real repository the moment it was written, not a regression
introduced by this sweep. No action taken on the historical commit
message itself (this project does not rewrite `git` history), but
**743 is the real, current, freshly-verified count**, and both
`ROADMAP.md` and `COMPARISON.md` should be read against that number
going forward, not 715.

**`COMPARISON.md` refreshed** (see its own file for the full text):
Phase 2's table row corrected from `22 | 0 | 22 | 0%` to
`22 | 3 | 19 | 14%`; Phase 3's from `8 | 0 | 8 | 0%` to `9 | 1 | 8 | 11%`
(row count also moved 8 to 9, row 3.9 itself being new since the last
sweep, not just its done count changing); the SQLite differentiator-
table row updated from a flat "No" to "Partial (`db_*`, row 2.15,
built)"; the "every Phase 2/3 differentiator is a plan" bullet and the
top-of-file summary paragraph both reworded to name the three real
Phase 2 exceptions plus row 3.9; and the closing regression-count
mention updated from a stale 727 to 743. A new dated "Re-swept
2026-08-22" note was added to the file's own header block rather than
silently overwriting the 2026-08-21 note it corrects, matching this
file's own established convention of layering dated corrections rather
than erasing prior ones.

---

**Recommendation for the next session, written here in full so it
survives independently of this conversation (the prior session's own
equivalent recommendation was given only in chat and was lost
afterward, a real, named gap in this project's own history -- this
entry exists specifically so that does not happen again).**

**Pick: continue Phase 2, specifically building row 2.1's (world-level
statedump) already-designed first slice next. Do not pick up Phase 1's
remaining low-evidence items for completeness, and do not pick up
`notes/ACCOUNT_LOGIN_PLAN.md` under the assumption it still has open
scope waiting -- both were checked directly this session, not assumed,
and both turned out weaker candidates than they looked from the prompt
alone.**

**Candidate 1: closing Phase 1's remaining low-corpus-usage items
anyway, for completeness. Rejected, on this project's own established
precedent, not a fresh judgment call.** Every item left on row 1.7 and
all of row 1.8 was deliberately deferred by an earlier session
specifically *because* it had zero or near-zero real corpus evidence,
using the exact same discipline this project has now applied
repeatedly and explicitly: `bind_lambda()`'s cross-object form rejected
as a stand-in (row 1.7's own 2026-08-17 decision, "(c)", no partial
stand-in built on invented demand), row 1.9's five mapping-width
sub-items left open on zero evidence, row 1.8 deferred the moment its
own fresh corpus check came back zero. Building any of row 1.7's
remaining items or row 1.8 now, for completeness rather than evidence,
would directly reverse a standing, repeatedly-reaffirmed project
discipline, not extend it. The one partial exception worth flagging for
whoever picks this area up again, not built this session: this sweep
found `inaugurate_master()`'s own arg=1/2/3 cases have never actually
been corpus-checked at all (every other row 1.7 item has a fresh
zero-or-near-zero finding on record; this one does not), so a future
session should run that check specifically before either building it or
formally recording it as zero-evidence-deferred like its neighbors --
that is a real, bounded, half-day investigation task in its own right,
not a reason to reopen this candidate broadly.

**Candidate 2: picking up `notes/ACCOUNT_LOGIN_PLAN.md`. Available in
principle but a materially weaker pick than the prompt's own framing
implied, confirmed by reading the file directly rather than assumed
still-open from its filename.** The file's own header states plainly:
"build ordering items 1 through 5 are real -- this plan's own originally
scoped build ordering is closed out" (dated 2026-08-21, a session
before this one). Account creation, login, password checking, character
creation with race-safe name reservation, and character selection are
all implemented, tested, and live-verified end to end -- there is no
remaining item in this plan's own original five-item scope to "pick up
where it left off." The file does name one genuinely open thread beyond
that original scope, "character management" for an already-authenticated
account (switching between existing characters, not just selecting one
at login) -- but its own text is explicit that this is "a natural
candidate for a future session's own fresh scoping pass," not
scoped work sitting ready to build. Picking this up would mean running
a cold-start scoping investigation first, the same real cost as picking
a fresh, previously-unscoped Phase 2 row -- it does not have the
"momentum already built up" advantage the candidate framing implies.
Also worth noting for whoever revisits this file: its own closing
"Explicit non-status" section (the file's last few lines) still reads
"Nothing above has been implemented," left over unedited from the
file's original 2026-08-19 draft and now flatly contradicted by its own
header two sections above -- a stale-prose loose end, not a status
signal, worth a one-line fix whenever this file is next touched, not
urgent enough to justify a dedicated session on its own.

**Candidate 3: beginning real Phase 2 planning-to-code work. Picked,
and picked specifically at row 2.1 rather than any other open Phase 2
row, on the following real evidence:**

- **Momentum and precedent already exist here, unlike either other
  candidate.** Three Phase 2 rows (2.9, 2.12, 2.15) already landed real,
  tested, live-verified code in the immediately preceding session,
  proving the "cold-start scope, then build" pattern this project uses
  works for this phase specifically, not just Phase 0/1. Candidate 1 is
  being deliberately *not* extended past its own evidence limit;
  candidate 2's own real momentum already fully spent itself on 2026-08-21.
  Phase 2 is the only one of the three with real forward motion still
  live.
- **Row 2.1 specifically, not an arbitrary Phase 2 row, because it is
  the one already carrying a genuine first-slice design, not just a
  scoping note.** A dedicated cold-start scoping session earlier today
  (see this file's own immediately-following entry, "Tier 2 cold-start
  scoping session") picked row 2.1 over 2.5/2.11/3.3 on real evidence
  (gates the most real follow-on work, rows 2.2/2.3/2.4 all block on
  it; lowest architectural blast radius of the four, a new empty module
  rather than surgery on `VM::run()`; no new external dependency, unlike
  2.11's LLVM requirement) and wrote a concrete, implementable design
  directly into `ROADMAP.md` row 2.1's own cell: an id-table-based
  two-pass serializer, reusing the already-existing `LiveObjectRegistry`
  for enumeration and extending the already-working `serializeValue()`/
  `deserializeValue()` tag format rather than inventing a new dependency.
  What remains for row 2.1 is real implementation work against an
  already-settled design, not another scoping pass -- a materially
  different, lower-risk starting position than every other open Phase 2
  row, all of which are still at the single-paragraph scoping-note stage
  only.
- **Matches this project's own stated end goal more directly than
  either other candidate.** `ROADMAP.md`'s own opening line states the
  actual goal is "the best LPC runtime available... surpassing both [FluffOS
  and LDMud] on the dimensions neither addressed" -- Phase 2 *is* that
  goal; Phase 1 is dialect-compatibility table stakes, already 91% done,
  and candidate 1 would spend a session pushing a table-stakes number
  from 91% toward 100% on evidence this project has repeatedly said does
  not justify the work. Candidate 2 is real, valuable mudlib content but
  not the project's own stated differentiator.

**Not started this session** (docs-only per this sweep's own scope; row
2.1's own first-slice design was written in the earlier session today,
not this one) -- implementing row 2.1 is the explicit, on-record next
step for whoever picks this project up next, LLM or human, without
needing this conversation's own context to know why.

---

**2026-08-22: Tier 2 cold-start scoping session, row 2.1 (world-level
statedump) picked over 2.5 (coroutine scheduler), 2.11 (LLVM JIT), and
3.3 (generational GC). Docs-only session per the prompt's own
instruction -- no implementation code written, ROADMAP.md row 2.1's own
scaffolding text replaced with a concrete first-slice design. 743 tests
still passing (verified by rebuilding and running `build/test/amlp_tests`
directly, not just trusting the last-known count), unchanged from the
prior session -- this was expected, no code touched this session.**

**Why 2.1 over the other three, weighed on real evidence, not just
listed order:** 2.1 gates the most real follow-on work of the four --
rows 2.2, 2.3, and 2.4 all explicitly block on its format, each
confirmed directly in its own ROADMAP.md note, not assumed. It also
carries the least architectural risk to start without a full plan: its
first slice lives entirely inside a new, currently-empty module
(`src/persist` holds only `instruct.md` today), not inside `VM::run()`
(2,743 real lines, exercised by all 743 of this project's own real
passing tests today -- 2.5's own note still cited 727, a count that has
already moved on since 2026-08-21) the way 2.5's `Suspend`-opcode work
would need to. And it needs no new external dependency, unlike 2.11
(LLVM 17+ against a driver whose entire real dependency footprint today,
re-confirmed directly against `CMakeLists.txt` this session, is
`pcre2-devel` + `libxcrypt-devel` + `sqlite3`). 3.3 was re-checked too,
not skipped on the strength of last session's own words alone: its cited
525 real `shared_ptr<LpcObject>`/`Array`/`Mapping`/`Closure` hits are 546
now, a direct re-grep this session -- confirming, not just repeating,
that row's "likely larger than its own instruct.md estimate" flag. None
of the other three had a lower-risk, more-bounded story than 2.1 once
actually compared side by side against real source, so 2.1 got this
session's real scoping pass.

**Real scope findings, from reading the actual architecture, not the
abstract feature:** the 2026-08-21 scoping pass had already correctly
identified reference identity as the real hard sub-problem (`save_object()`'s
own working `serializeValue()`/`deserializeValue()`, `EfunTable.cpp`,
drops object references and closures to void on write -- fine for one
object's own save file, wrong for a world snapshot where two saved
objects sharing a reference to a third live object need to resolve back
to the *same* restored object). This session found two concrete things
that pass missed, and both changed the actual first-slice shape:

1. Enumeration is already solved. `LiveObjectRegistry` (`include/amlp/object/LiveObjectRegistry.hpp`,
   `src/object/LiveObjectRegistry.cpp`) is a real, already-working global
   `weak_ptr<LpcObject>` registry, populated by `ObjectManager::loadObject()`/
   `cloneObject()` already (confirmed directly in `ObjectManager.cpp`) and
   already backing the live `objects()`/`livings()` efuns.
   `LiveObjectRegistry::all()` is exactly the "walk every live object,
   blueprint or clone" primitive a world dump's enumeration pass needs --
   nothing new to build there.
2. Object placement is a second, separate reference-identity problem the
   prior note never named. `LpcObject::environment_`/`::inventory_` are
   driver-internal placement fields, not part of `variables()` --
   `save_object()`'s own precedent has never had to serialize them at
   all. A world dump that drops room/inventory placement is not really a
   world dump, so the first slice includes it, reusing the same id-table
   mechanism variables need rather than a second one.

Also confirmed directly, not just re-asserted: `src/persist/instruct.md`'s
own CBOR/`nlohmann::json` choice has no real dependency behind it
anywhere in this repo's actual build -- no `find_package`/`pkg_check_modules`
for it in `CMakeLists.txt`, no vendored header under `include/`/`src/`
(the only real hits are other unbuilt `instruct.md` proposals and one
copy vendored inside `temp/fluffos/` that this build does not link).

**Concrete first-slice proposal (written into ROADMAP.md row 2.1, not
left in chat only):** a new `StateSerializer` in `src/persist`, id-table
based, two dump sections written in reconstruction order -- section A
(id, filename, isClone, environment id, inventory ids) lets restore
fully rebuild every object's identity and placement via the *existing*
`lookupLoadedObject()`/`loadObject()`/`cloneObject()` paths before
section B (each object's `variables()`, via a `serializeWorldValue()`
that extends the existing `I`/`F`/`S`/`A`/`M`/`N` tag scheme with `O<id>`
for object references and `C` for ordinary closures) ever needs to
resolve an id. Explicitly deferred out of this slice, each independently
bounded and non-blocking: `Scheduler`'s call_outs/heartbeats (already
isolated, read-only-accessible state), `actions_`/shadow/snoop fields
(mechanically the same id-table treatment, just more of them), parse-info/
`totalLight_`, and reset/cleanup timing bookkeeping (restored fresh via
the existing `armResetAndCleanup()` path, matching real-driver-reboot
practice). Full reasoning, including the exact file-format tag layout
and the closure/`unbound_lambda()` failure-mode decision, is in
ROADMAP.md row 2.1 itself now, not just here.

**2026-08-21 (same session as row 2.12 immediately below, continuing
with session time left per that session's own prompt): row 2.9 (apply
cache), the second Tier 1 pick. Confirmed real scope from source before
writing any code, and it changed this row's actual shape -- not a
dialect mismatch this time, an architecture-fidelity one.**

**Real finding: `src/apply/instruct.md`'s own literal design sketch
(`unordered_map<pair<LpcObject*,string>, FunctionEntry*>`, manual
invalidation on recompile and on `destructObject()`) does not match how
this driver's real architecture actually works.** Read `findFunctionInChain()`
(`VM.cpp`) and `ObjectManager::compile()`/`cloneObject()` directly rather
than trusting the sketch or the prior scoping session's "confirmed still
accurate" framing: `findFunctionInChain()` takes a `const
CompiledProgram&`, not an `LpcObject*` -- its result depends only on
program identity, and `ObjectManager::programCache_` (keyed by filename)
hands the identical `shared_ptr<CompiledProgram>` to every clone of a
blueprint (confirmed directly), so an `LpcObject*`-keyed cache would
populate one redundant entry per clone for what is provably always the
same answer -- a real, measurable loss against the row's own cited
motivation (many living clones sharing one blueprint's `heart_beat()`).
Worse than just suboptimal: `ObjectManager::compile()`'s own recompile
branch, read in full, never mutates an existing `CompiledProgram` in
place -- a recompile always `std::make_shared`'s a brand new instance
(confirmed via that function's own comment, already regression-tested by
row 0.15's `testCloneObjectRecompilesWhenSourceChangesEvenWithoutAnIntermediateLoadObjectCall`).
An external map keyed by raw `CompiledProgram*` would therefore risk a
genuine dangling-pointer read once every real owner of some old program
let go and nothing pruned that map's own entries first -- a real memory-
safety hazard the literal sketch's own two-event invalidation plan was
implicitly trying to head off, not just an inefficiency.

**Built:** a `functionChainCache_` member directly on `CompiledProgram`
itself (`Bytecode.hpp`, new nested `FunctionChainCacheEntry` struct plus
a `mutable std::unordered_map<std::string, FunctionChainCacheEntry>`,
with a long inline citation comment carrying the full reasoning above)
rather than an external map. This one change resolves every concern at
once: the cache's lifetime is now identical to the program's own by
construction, so a fresh recompile automatically starts with an empty
cache (nothing to explicitly invalidate) and an old, superseded
program's own cache is destroyed together with the rest of that no-
longer-referenced program, never separately from it -- so neither
"invalidate on recompile" nor "invalidate on `destructObject()`" needed
any code at all; both are structurally guaranteed by composition
instead. `replace_program()`'s own live `ob->setProgram(...)` hotswap
(`VM.cpp`, LDMud dialect) is handled correctly for free too, since every
lookup re-reads `obj->program()`'s current pointer fresh rather than
ever caching a stale one. `findFunctionInChain()` itself (`VM.cpp`)
rewritten to check/populate the per-program cache, preserving its exact
original recursive resolution order (own functions first, then each
`inheritedPrograms` entry depth-first, first match wins) -- confirmed by
reading the diff side by side, not just by the tests passing.
`findParentFunction()` (the separate, colder `::name()`/
`qualifier::name()` explicit-parent-call path) deliberately left
uncached, out of scope this slice: real, already-shipped motivation
(`Scheduler.cpp`'s own real `call_heart_beat()`-equivalent,
`vm_.callFunction(obj, "heart_beat", {})` on every living object every
real tick) only ever bottoms out in `findFunctionInChain()`, confirmed
unchanged from the prior scoping session's own citation.

**3 new regression tests** (743 total, up from 740 -- row 2.12,
immediately below in this same session, took the count from 733 to 740
first): warms the cache on an object holding the *old* program across a
real recompile and confirms both the old object's own warmed entry and a
freshly cloned object against the *new* program stay correct with zero
explicit invalidation; two clones of one blueprint sharing one
`CompiledProgram` both correctly resolve an inherited function through
the same shared cache entry (the exact real win the per-program keying
decision above was made for); `functionExists()`'s own negative-cache
path (both pointers null, a real, valid "confirmed absent" state, not an
error -- most objects define no `reset()`/`clean_up()`/`heart_beat()` at
all, and this driver's own scheduler re-asks that every tick just as
often as a real hit) stays consistently false across repeated asks and
does not corrupt a later real hit on a function that does exist.

Live-verified against the real running driver (`./build/amlp
etc/driver.cfg`) and the real bundled mudlib over a real TCP session: 5
repeated `eval` calls (each recompiling `/tmp_eval_file` fresh, stressing
per-program cache creation/discard under rapid, real repeated
recompilation, matching `eval.c`'s own real rm-then-write_file-then-
reload cycle) all returned correct results (`0, 1, 4, 9, 16`), followed
by 3 real heartbeat intervals (~7s at the configured 2000ms interval)
elapsing on a live connected player object -- `mudlib/clone/user.c` has
`set_heart_beat(1)` active with no `heart_beat()` function defined
anywhere in its own real inherit chain, confirmed directly by grep
across the whole mudlib, so every real tick during that wait exercised
the negative-cache path specifically, not a hypothetical one -- with a
final `eval return 6 * 7;` afterward still correctly returning 42 and the
driver's own log showing no errors, crashes, or exceptions across the
whole session. Test-account/character state created during that
verification deleted afterward, matching row 2.15/2.12's own established
cleanup precedent.

**2026-08-21 (a further fresh session, later than every session below,
resuming after a disconnect): row 2.12 (`pcre_match`/`pcre_assoc`), the
second Tier 1 pick, picked up right where the disconnected session's own
prompt left off. Confirmed real scope from source before writing any
code, the same discipline every prior row has used, and it changed what
this row actually is, the same shape row 2.15's own correction took.**

Oriented fresh per this session's own instructions: read `CLAUDE.md`
(confirmed both non-negotiable rules), then confirmed row 2.15's own
work was already committed and pushed -- by the project owner directly
(`git log`: author `Thurtea <itsthurtea@gmail.com>`, commit `767e0ba`),
not by this session, consistent with the `git add`-only rule never
having been violated.

**Real finding, before any implementation: this row's own "FluffOS
`pcre_*`-family efun names" framing is real, but not from the tree this
repo cites for everything else.** The pinned `temp/reference/
fluffos-2.9-ds2.08` tree (`CLAUDE.md`'s own named citation source) has
no PCRE package at all -- confirmed via a plain "pcre" text grep across
the entire tree, zero hits anywhere; `func_spec.c`/`efun_defs.c` list
only `regexp()`/`reg_assoc()`, backed by a bundled Henry Spencer engine,
not PCRE (the same finding the `EfunTable.cpp` Phase 0 row 0.11 comment
already documented). The real `pcre_*` family instead lives in a
*different*, separately-vendored FluffOS source tree already present in
this repo, `temp/fluffos` (a modern/master checkout): `src/packages/
pcre/pcre.spec`/`pcre.cc`/`src/include/pcre_flags.h` -- a package that
only exists in FluffOS history after it moved off the Henry Spencer
engine onto real PCRE, well after the pinned 2.9-ds2.08 patchlevel.
Documented explicitly as a divergent citation source in both `EfunTable
.cpp`'s new comment block and this row's own `ROADMAP.md` entry, rather
than silently treated as 2.9-ds2.08 evidence -- the same kind of named,
explicit dialect correction row 2.15's LDMud-vs-FluffOS finding made,
just a version-lineage split within FluffOS itself rather than a
different driver family entirely.

**Real call-site evidence swept across every vendored mudlib corpus**
(`core-lib`, `dead-souls`, `es2_mudlib`, `lima`, `nightmare3`,
`reference-lpc-mud-library`): of the real package's eight efuns
(`pcre_version`/`pcre_match`/`pcre_match_all`/`pcre_assoc`/
`pcre_extract`/`pcre_replace`/`pcre_replace_callback`/`pcre_cache`),
only `pcre_assoc()` and `pcre_match()` have any real call sites anywhere
-- both in the vendored `lima` corpus (`lib/daemons/xterm256_d.c`,
`lib/std/modules/m_frame.c`, filtering/tokenizing ANSI colour-code
text). The other six efuns have zero call-site evidence in any vendored
corpus and are out of scope this slice, the same bounded-to-real-
evidence discipline row 2.15 used for `db_affected_rows`/`db_insert_id`/
`db_coldefs`.

**Built:** `pcre_match()`/`pcre_assoc()` (`EfunTable.cpp`) as PCRE-backed
drop-in analogs of the already-existing `regexp()`/`reg_assoc()`
(confirmed directly against `pcre.cc`'s own real header comment, "analog
with regexp efun ... for backwards compatibility reasons but utilizing
the PCRE library", and the real upstream docs for both efuns) -- same
selection/tokenizing contracts (string-form 1/0, array-form matching-
elements-with-optional-index/invert for `pcre_match()`; identical
earliest-match-wins multi-pattern scan for `pcre_assoc()`), plus a new
optional `pcre_flags` bitmask argument neither existing efun has. Flag
values and their compile/exec-option mapping cited directly from
`pcre_flags.h` and `pcre.cc`'s own `compute_compile_options()`/
`compute_exec_options()`: `PCRE_I`/`PCRE_M`/`PCRE_S`/`PCRE_U`/`PCRE_X`
are compile-time (mapped onto PCRE2's own `PCRE2_CASELESS`/
`MULTILINE`/`DOTALL`/`UNGREEDY`/`EXTENDED`), `PCRE_A` is exec-time only
(`PCRE2_ANCHORED`). Real `compute_compile_options()` also always ORs in
`PCRE_UTF8` unconditionally regardless of flags -- ported as PCRE2's own
renamed `PCRE2_UTF` (confirmed directly against the linked `pcre2.h`:
PCRE2 has no `PCRE2_UTF8` constant at all) -- a real, deliberate
divergence from `regexp()`/`reg_assoc()`/`regexplode()`, which stay
byte-oriented exactly as Phase 0 row 0.11 built and tested them; only
these two new efuns validate UTF. `compileRegex()`/`regexFindNext()`
(row 0.11's own shared helpers) both gained optional trailing options
parameters defaulting to 0, so every pre-existing caller's behavior is
unchanged.

`__PACKAGE_PCRE__` and six `PCRE_*` flag constants added as new driver-
injected compiler predefines (`ObjectManager.cpp`'s
`buildPredefinedMacroFlags()`), in a new, separately-commented table
explicitly kept apart from the existing verbatim-copied-from-2.9-ds2.08
predefine tables, since these aren't from that tree -- cited instead to
`temp/fluffos`'s own `option(PACKAGE_PCRE ...)` plus its testsuite's own
`#ifdef __PACKAGE_PCRE__` convention, matching the same `__PACKAGE_X__`
pattern this driver's existing tables already use for its own real
packages. Values given as pre-computed plain decimal literals rather
than `(1 << N)`-shaped expressions: the `-D` flags are all assembled
into one unquoted `popen()` shell command string, and unescaped parens
there open a bash subshell rather than just getting word-split -- the
same real constraint the existing `__VERSION__` entry's own comment
already documents for embedded spaces, caught before it ever reached a
build.

**6 new regression tests, actually 7:** (740 total, up from 733):
`pcre_match()` string-form match/no-match; its 3rd argument being a
real, legal `pcre_flags` (`PCRE_I` turning a would-otherwise-fail match
into a match, unlike `regexp()`'s own real hard error there); its real
4-arg-in-string-form illegality; array-form index/invert selection
(identical to `regexp()`'s own, reusing that test's own fixture data);
a real all-4-argument array-form call (the exact shape `pcre_match.lpc`
's own real upstream testsuite regression-tests against a historical
wrong-stack-slot driver bug -- confirmed moot for this driver, since its
own calling convention passes efun arguments as a plain vector rather
than a raw interpreter stack, documented inline rather than reproduced
as a bug); `pcre_assoc()` reproducing `reg_assoc()`'s own real worked
doc-comment example against an all-uppercase subject with and without
`PCRE_I`, confirming the flag is actually threaded into every pattern's
own compile options rather than merely accepted and silently ignored.

Live-verified against the real running driver (`./build/amlp etc/
driver.cfg`) and the real bundled mudlib over a real TCP session, via
the bundled `eval` command after a real account-creation ->
character-naming login flow: all seven probes (`pcre_match()` string
form plain and with `PCRE_I`, array form plain and inverted,
`pcre_assoc()` with and without `PCRE_I` against an uppercase subject,
plus `__PACKAGE_PCRE__` resolving as an empty-value feature-guard macro
the same way `__PACKAGE_SOCKETS__` etc. already do) returned exactly
the expected real values. Test-account/character state created during
that verification (`mudlib/accounts/p/pcretestacct.o`, `mudlib/
characters/p/pcretestchar.o`) deleted afterward, matching row 2.15's own
established cleanup precedent.

**2026-08-21 (a fresh session, later than every session above): row
2.15 (SQLite/`db_*` efuns), the top Tier 1 pick from the prior Phase 2
scoping session. Confirmed real scope from source before writing any
code, the same discipline used throughout this project, and it changed
what this row actually is.**

Oriented fresh per this session's own instructions: read `CLAUDE.md`
(confirmed both non-negotiable rules: `git add` only, no commits/pushes;
no em dashes or emojis).

**Real finding, before any implementation: `core-lib` (this row's own
cited evidence) is not a FluffOS corpus, it is an LDMud corpus, and the
`db_*` efun family it uses is LDMud's own real family, not FluffOS's.**
`core-lib/README.md` states directly it "targets the LDMud driver."
Checked real vendored FluffOS 2.9 (`temp/reference/fluffos-2.9-ds2.08/
packages/db.c`) directly rather than assuming the row's own "SQLite
built-in" title implied a FluffOS-shaped implementation: real FluffOS's
db package has a `db_connect(host, database, user, type)` signature
LDMud's never had, `db_exec` returns rows-affected-or-an-error-*string*
rather than a handle-or-zero, `db_fetch(handle, row)` is row-indexed,
there is no SQLite backend of any kind (only mSQL/MySQL), and no
`db_error`/`db_handles`/`db_conv_string` efuns exist at all. This
project also has a real vendored LDMud source tree (`temp/ldmud`),
confirmed via `temp/ldmud/doc/efun/db_*` and `temp/ldmud/src/
pkg-mysql.c` directly: an exact match for every one of core-lib's own
real call sites -- `db_connect`/`db_exec`/`db_fetch`/`db_close`/
`db_error`/`db_handles`/`db_conv_string`, confirmed all seven genuinely
called somewhere in core-lib; `db_affected_rows`/`db_insert_id`/
`db_coldefs` also real LDMud efuns, confirmed zero call sites anywhere
in core-lib, so not built this slice, per this session's own explicit
instruction to bound scope to exactly what the real evidence uses.
Built the real LDMud family (signatures, return shapes, and error
semantics all cited directly from `pkg-mysql.c`, not guessed), backed
by SQLite instead of a live MySQL server -- a deliberate engine
substitution this session made explicitly and documented inline rather
than silently assuming, since SQLite was always this project's own
lighter-dependency preference (the prior scoping session's own note),
not something any corpus asks for by name. Registered unconditionally
(not gated behind `dialect == "ldmud"`): there is no competing
FluffOS-named `db_connect` this driver also implements to disambiguate
against the way `valid_read`/`valid_write`'s dialect gate exists for,
and real LDMud's own "OPTIONAL, available only if compiled with mySQL
support" gate maps onto "always registered, since SQLite is always
linked now" with `privilege_violation("mysql", ...)` as the real
per-call access gate, exactly matching upstream's own `check_privilege()`
-> `master->privilege_violation("mysql", efun_name)` pattern (confirmed
every one of the seven real call sites in `pkg-mysql.c` passes
`MY_TRUE` for `raise_error`, i.e. a denial hard-errors, it does not
silently no-op) -- `db_conv_string` is the one real exception, its own
real source has no `check_privilege()` call at all, ported the same way
here.

**Dependency: `sqlite-devel` was not installed on this Fedora
workstation**, confirmed directly (`pkg-config --modversion sqlite3`
failed, `rpm -q sqlite-devel` reported not installed, `find /usr/include
-iname sqlite3.h` found nothing -- only the runtime `sqlite-libs`
package was already present). Installed via `sudo dnf install
sqlite-devel` (now resolves, version 3.51.2). `src/efun/CMakeLists.txt`
gained `pkg_check_modules(SQLITE3 REQUIRED sqlite3)` alongside the
existing `PCRE2` line, matching that exact established convention
precisely as this session's own instructions asked; `INSTALL.md`'s own
prerequisite list and both Fedora/Debian package-install lines updated
to match, the same kind of correction this project's own prior Phase 2
scoping session flagged for `nlohmann/json` elsewhere but did not itself
fix.

**Built:** new `DbRegistry` class (`include/amlp/efun/DbRegistry.hpp` +
`src/efun/DbRegistry.cpp`), a monotonic-handle global registry matching
`SocketRegistry`'s own established precedent in this driver (not real
LDMud's own chained-list slot-reuse allocator -- a real implementation
detail with zero LPC-visible contract depending on it). Real per-quirk
fidelity worth naming explicitly, not just "ported the signatures":
`db_fetch()`'s row values are all formatted as strings, even for a
SQLite-native integer/float column, matching real MySQL's own C client
API returning every column as raw text regardless of underlying type
(`f_db_fetch()`'s own unconditional `put_c_string()`, confirmed by
direct read, not assumed from the doc's "mixed" return type alone);
`db_conv_string()` doubles a single quote (SQLite's own literal-escaping
rule) rather than real MySQL's backslash-escape convention -- a
necessary, explicitly documented divergence, since a MySQL-shaped
escape is not valid SQLite syntax and this helper's entire real purpose
is producing safe input for whatever backend is actually running.
Seven efuns registered in `EfunTable.cpp`: `db_connect`, `db_exec`,
`db_fetch`, `db_close`, `db_error`, `db_handles`, `db_conv_string`.

**Tests:** 6 new regression tests in `test/test_lexer.cpp`, 733 total
(up from 727, confirmed unchanged going in): a full connect/exec/fetch/
close/`db_handles` round trip against a real scratch SQLite file
(covering a `CREATE TABLE`, two inserts, a `SELECT`, sequential
`db_fetch()` reads, and the exhausted-result 0 return); `db_error()`
returning 0 after a successful statement and the real error message
after a genuinely bad one (also confirming `db_exec()` itself returns 0
rather than throwing for a bad-SQL failure, matching real "just an
error in the SQL-statement" semantics, not a hard error); `db_conv_string()`'s
real escaping behavior and its confirmed lack of privilege-gating
(mirroring this driver's own existing `bind_lambda` privilege-violation
test precedent exactly, including a no-master-lfun-at-all hard-error
case); a denied-privilege hard-error case; an unknown-handle hard error
shared by every gated call (`pkg-mysql.c`'s own `errorf("Illegal handle
for database.\n")`, confirmed the same across every real call site
checked).

**Live verification:** `mudlib/single/master.c` gained a permissive
`privilege_violation()` lfun -- this file previously had none at all,
meaning no `db_*` call could ever have been exercised live against this
driver's own bundled mudlib before this row; matches that file's own
already-permissive `valid_bind()`/`valid_hide()` character rather than
introducing new laxness. Booted the real driver (`./build/amlp
etc/driver.cfg`) and drove a real TCP client through this mudlib's own
real login flow (account creation, password confirmation, character
naming) into a real `eval` command session: `db_connect` to a real
scratch SQLite file, `db_exec` a `CREATE TABLE` and two `INSERT`s (one
routed through `db_conv_string()` with a real embedded apostrophe,
`O'Brien`), `db_fetch` both rows back correctly (apostrophe intact,
alphabetical `ORDER BY` order confirmed), `db_error` reading 0 after the
successful sequence, `db_close`. A separate check confirmed two
simultaneously open connections tracked independently via `db_handles()`
(`[1, 2]`, then `[2]` after closing only the first). Test-account/
character save state created during this live verification
(`mudlib/accounts/`, `mudlib/characters/`, both git-confirmed fully
untracked/nonexistent beforehand) deleted afterward, matching row 3.9's
own already-established precedent for live-verification cleanup rather
than inventing a new convention.

733 tests passing, up from 727 -- confirmed by running the suite
directly both before touching any code and again after, not assumed.

**2026-08-21 (a fresh session, later still): the Phase 2 cold-start
scoping session recommended and deferred for several sessions running.
Read every Phase 2 row's own `src/*/instruct.md` plus `src/gc/instruct.md`
(explicitly requested alongside Phase 2 despite living under Phase 3)
against this driver's actual current architecture, not the abstract
version of each feature. Scoping only, no implementation, matching the
same discipline `privilege_violation()`/`parse_*` each got before any
code was written for either. All 22 Phase 2 rows plus row 3.3 (GC) in
`ROADMAP.md` rewritten from one-line stubs to real, cited breakdowns.
727 tests, unchanged, this is a docs-only session.**

Oriented fresh per this session's own instructions: read `CLAUDE.md`
(confirmed both non-negotiable rules: `git add` only, no commits/pushes;
no em dashes or emojis).

**Two findings that apply across the whole phase, not just one row,
found while reading rather than assumed going in.** First: the
`src/*/instruct.md` files for Phase 2 are not uniformly current.
`src/gc/instruct.md`'s own testing section cites "374 tests" as the
regression baseline to protect -- this repo is at 727 today, meaning
that file predates roughly half of this project's own real history,
confirmed stale by a concrete number, not an impression. `src/persist/
instruct.md` and `src/lsp/instruct.md` both assert `nlohmann/json` is
"already a dependency"; grepped every real `CMakeLists.txt` in this
repo directly, it is not, anywhere, and `INSTALL.md`'s own confirmed
prerequisite list (`pcre2-devel`, `libxcrypt-devel` only) does not
mention it either -- both files need that correction whenever either
row is actually picked up. `src/apply/instruct.md`, by contrast, carries
real, dated 2026-08-22 corrections cross-referencing actually-completed
Phase 1 rows accurately: the staleness is per-file, not uniform, exactly
matching `CLAUDE.md`'s own standing warning about these files, now with
concrete evidence behind it rather than just the warning itself. Second:
Phase 2 is not corpus-driven the way Phase 0/1 was, by this project's
own repeated prior framing, and a fresh corpus check this session
confirms why for most of it: zero real hits for `json_encode`/
`json_decode`/`http_get`/`http_post` anywhere in `temp/`, and every
`hash(`-substring hit checked was a `rehash`/`eventRehash` false
positive, not a real efun call. Two real exceptions were found, and they
matter for the recommendation below.

**Row-by-row findings, full detail in `ROADMAP.md` itself (each row's
own cell), summarized here rather than duplicated in full.**

- **2a Persistence (2.1-2.4):** 2.1's own real hard problem, not named
  in its instruct.md, is reference identity -- a world snapshot cannot
  reuse `save_object()`'s own "object references don't survive a save"
  shortcut, since two saved objects referencing a third live one need
  that reference to resolve to the *same* restored object. A real,
  unsolved design problem, not an extension of existing code. Large,
  multi-session. 2.2 depends on 2.1's format; its own real risk is
  integration into `ObjectManager`'s cache lookup, the hottest path in
  this driver. Medium-large. 2.3 (hotboot) has zero `fork()`/`exec()`
  precedent anywhere in `src/net` today, and explicitly depends on 2.1.
  Large, gated. 2.4 is mostly integration testing once 2.1 exists;
  small, not independently schedulable.
- **2b Concurrency (2.5-2.8):** 2.5 rearchitects `Scheduler::run()`
  (492 real lines) and needs new `VM::run()` (2,743 real lines) suspend
  support -- every one of 727 tests exercises `VM::run()`, the highest
  blast-radius change available in this phase. Large, highest risk. 2.6
  and 2.7 are real but fully gated on 2.5 landing first. 2.8 (Hydra) has
  a real, previously-undocumented hard dependency, found this session:
  its own instruct.md cites row 1.12's checkpoint/rollback mechanism,
  confirmed still `[ ]` and itself permanently deferred as a zero-
  evidence, comparison-only DGD row. Effectively blocked, not just large.
- **2c Apply cache + JIT (2.9-2.11):** 2.9 is the most concretely
  scoped row in all of Phase 2, and has real, already-shipped
  motivation: `heart_beat()` already walks `findFunctionInChain()` on
  every living object every tick, live since 2026-08-07. Small-medium,
  real evidence. 2.10 was already investigated and answered by an
  earlier session, not something this row needs to re-derive --
  `Value.hpp:160-174`'s own closure-design comment concludes eager vs.
  lazy resolution are "behaviorally identical for anything this driver
  runs." No correctness motivation, no measured performance motivation
  anywhere in this project. **Recommend not building until real
  evidence exists**, the same standard applied to every zero-evidence
  Phase 1 row. 2.11 (JIT) needs LLVM 17+, by far the largest new
  dependency proposed anywhere in Phase 2 (this driver's entire current
  real dependency footprint is `pcre2-devel` + `libxcrypt-devel`). Its
  own stated precondition, "bit-identical output for every test," is a
  real if soft gate. Genuinely unknown size, the single largest and
  riskiest item in the phase.
- **2d Efun breadth (2.12-2.18):** 2.12 is the smallest-delta row in
  all of Phase 2 -- `pcre2-devel` is already linked, `regexp()`/
  `regexplode()`/`regexp_assoc()` already real and working; this is
  extending a proven pattern, not standing up anything new. 2.13 (TLS)
  needs a new OpenSSL dependency, contained to `src/net`. 2.14
  (WebSocket) has **the strongest real corpus/deployment evidence found
  anywhere in Phase 2**: `temp/dead-souls/config.deadsouls` and
  `temp/nightmare3/nm3.cfg` both configure a real production websocket
  listening port, and `dead-souls/lib/www/example.js` ships real
  browser JS expecting it to work; also found, worth deciding
  explicitly rather than assuming: its own instruct.md body never
  actually references TLS despite the section title, a plain `ws://`
  implementation may not need 2.13 first at all. 2.15 (SQLite) has
  **the single strongest real mudlib-code-level evidence in this whole
  phase**: `temp/core-lib`'s own README and docs describe `db_connect`/
  `db_exec`/`db_fetch`/`db_error` directly as real driver efuns, and two
  real files under `lib/modules/secure/dataServices/` contain genuine
  `db_exec()` call sites -- a corpus this project already treats as its
  primary LDMud reference directly depends on this efun family. SQLite3
  is a far lighter dependency than OpenSSL or LLVM. 2.16 (hash efuns)
  found this driver's own real hashing need already solved by `crypt()`
  (this project's own shipped `account_d.c`); the specific SHA/MD5/
  BLAKE2/bcrypt family has zero direct corpus evidence, `bcrypt` as an
  internal `crypt()` upgrade is a real want, not corpus evidence, and
  should be named as such if ever picked up. 2.17 (JSON) needs
  `nlohmann/json`, confirmed not currently a dependency (see the
  phase-wide finding above); zero external corpus evidence, real value
  is as a dependency for 2.18/2.22, not standalone. 2.18 (HTTP) is
  hard-blocked on 2.5/2.6 plus needs libcurl; zero corpus evidence.
- **2e Developer experience (2.19-2.22):** 2.19 (LSP) needs
  `nlohmann/json` (same correction as 2.17); the one row in this whole
  phase with a plausible **direct benefit to this project's own future
  development velocity** rather than corpus/deployment evidence, a
  different but real kind of justification worth naming explicitly. Its
  own instruct.md already gives a real bounded first slice (diagnostics
  only) distinct from the full large vision. 2.20 (structured errors)
  needs no new dependency by itself, could be hand-rolled; only real
  value realized paired with 2.19 or 2.22, speculative alone. 2.21
  (hot-reload) is narrower than its own instruct.md suggests, a real
  finding this session: `ObjectManager::reloadObject()` **already
  exists and is real** (`ObjectManager.cpp:978`+), already doing the
  hard, risky parts (shadow-chain safety, `LivingNameRegistry` safety,
  `create()` re-run) that real FluffOS `update` does; what is actually
  missing is narrower -- a force-recompile path and name-based variable
  migration layered on top, not a rebuild from scratch. Also found:
  `LivingNameRegistry` already stores `weak_ptr<LpcObject>`, so this
  row's own "swap `program_`/`variables_` in place on the same
  `shared_ptr`" plan needs no changes elsewhere. Medium, smaller than
  its own instruct.md implies. 2.22 (test runner) is small and
  mechanical for the core assertion efuns; only its own "JSON file for
  CI" detail touches the same dependency question as 2.17/2.19/2.20,
  and is separable from the rest.
- **Row 3.3 (GC), Phase 3, scoped alongside Phase 2 per this session's
  own explicit request:** confirmed stale the same concrete way as
  `src/gc/instruct.md`'s sibling files ("374 tests" vs. 727 today).
  Grepped every real `shared_ptr<LpcObject>`/`Array`/`Mapping`/`Closure`
  occurrence across `src/`+`include/` directly: 525 real hits today, a
  surface roughly double what existed when the instruct.md's own
  5-layer migration plan was written, meaning layer 4 (`LpcObject`
  itself, touching `ObjectManager`, `InteractiveRegistry`,
  `LivingNameRegistry`, `Scheduler`, and `Server` all at once per that
  file's own text) is almost certainly larger in real scope today.
  Already, by the instruct.md's own words, "the single most invasive
  change in the roadmap" -- not re-litigated this session, no evidence
  surfaced to soften that framing. Large, multi-session, likely larger
  now than its own estimate, the single riskiest item named in either
  phase this session touched.

**Recommended ordering, reasoning below, not just a list.** Three real
tiers, not a flat ranking of 22+1 items:

**Tier 1, real evidence, small-to-medium, buildable without a further
scoping session: 2.15 (SQLite) first, then 2.12 (PCRE extension) and 2.9
(apply cache) as low-risk companions or immediate follow-ons.** 2.15 is
the strongest real evidence found anywhere in this phase, in either the
mudlib-call-site sense (2.15) or the deployment-config sense (2.14) --
picked 2.15 over 2.14 specifically because it is mudlib-*code*-level
evidence (a real corpus's own real logic depends on it), a category
closer to how every Phase 0/1 priority call was actually made, and
because SQLite3 is a lighter, more common dependency than the
OpenSSL 2.14 more plausibly wants first (even though 2.14's own
instruct.md text does not strictly require TLS, per the finding above).
2.12 and 2.9 are not evidence-tied to 2.15 at all, named alongside it
only because they share the same real property that actually matters
for sequencing: genuinely small, genuinely bounded, buildable today with
no further design investigation, unlike everything below.

**Tier 2, real but each needs its own dedicated scoping/design session
before any code, the same discipline `privilege_violation()`/`parse_*`
each got: 2.1 (statedump's reference-identity design), 2.5 (the
coroutine scheduler rearchitecture), 2.11 (JIT's own toolchain
question), and row 3.3 (GC's migration plan against the real, larger-
than-estimated current codebase).** These four are not "small
follow-ons" to Tier 1 -- each is its own real, large, multi-session
body of work this single scoping pass correctly bounded but did not
(and should not) design in full. Whoever picks up Phase 2's own next
big investment after Tier 1 should treat each of these four as needing
the same kind of cold-start investigation this session itself was, not
jump straight to code from this session's own summary.

**Tier 3, real but either blocked on something else, or explicitly not
justified yet, or only valuable bundled with something else -- not
independently worth picking up on their own:** 2.2-2.4, 2.6-2.8, 2.13/
2.14/2.16-2.18, 2.19/2.20 (a real project-benefit case exists for 2.19,
but its own full vision is large; its own bounded diagnostics-only
slice is arguably Tier 1-adjacent if a future session wants exactly
that narrower scope), 2.21, 2.22. **2.10 and 2.8 specifically are
recommended against for now**, not merely deprioritized: 2.10 has an
earlier session's own direct finding that it changes nothing observable
for anything this driver runs, and 2.8 is architecturally blocked on a
permanently-deferred row (1.12) this project has already decided not to
build absent new evidence.

727 tests passing, unchanged -- this was a docs-only session, no source
touched, confirmed by re-running the suite after the `ROADMAP.md` edits
rather than assumed.

**2026-08-21 (a further session, continued the same day, the last
session-worth of the day): `notes/ACCOUNT_LOGIN_PLAN.md`'s build
ordering item 5, character selection, bounded to selection-only per
this session's own explicit permission to defer the larger "create new
character" half, closing out that plan's entire originally scoped
build ordering (items 1 through 5). Then a fresh, honest full-project
status sweep across Phase 0/1/2/3 (all five headline numbers confirmed
unchanged) and this session's own recommendation for what comes next,
written here rather than left in chat. 727 tests, up from 725. One
unrelated housekeeping note: `STATUS-ARCHIVE.md` was deleted by the
project owner directly between sessions (a real human commit, not
something this session did or is second-guessing); this file's own
header above was updated to stop pointing at it.**

Oriented fresh per this session's own instructions: read `CLAUDE.md`
(confirmed both non-negotiable rules: `git add` only, no commits/pushes;
no em dashes or emojis).

**Item 5, read against the current mudlib before building anything, per
this session's own instructions.** The plan's own "Proposed
architecture" section 4 describes character selection as "list existing
characters, pick one **or create new**" -- read carefully rather than
skimmed, since that "or create new" clause is exactly the kind of
implicit-scope-creep risk this session was asked to check for, the same
discipline that correctly recognized item 4's own "starting attributes"
clause as unbuilt speculative content rather than a real gap. Confirmed:
`account_d.c`'s `characters()` (built item 4) already returns the full
list, real and working; nothing in the shipped login flow can ever give
an account a second character, though -- `add_character()` is only ever
called once, from `got_character_name()`, itself only reachable from
brand-new-account creation. So "pick one" (this session's own real
scope) and "or create new" (deliberately not built) are genuinely
separable, not artificially split: the former needs only a read of an
already-real list and a menu; the latter needs a wholly new login-flow
branch reachable *after* a player is already authenticated and has
already selected once, plus an actual decision about whether/how to
limit repeated character creation, neither of which this session
invented an answer for. No deletion, no character cap, was mentioned or
implied anywhere in the plan's own text at any level (Proposed
architecture, Rough build ordering, or any earlier per-item writeup) --
confirmed by rereading the whole plan file fresh, not assumed absent
because convenient.

**Built:** `got_login_password()`'s own success branch now checks
`sizeof(chars) > 1` before falling back to `chars[0]`; when true, a new
`show_character_menu()` lists every character by number and registers
`got_character_selection()`. That function parses the reply via
`sscanf(str, "%d", choice)` (the same bare-variable-target idiom already
real and working in this mudlib, `master.c`'s own `retrieve_ed_setup()`),
validates the range against a stashed `pending_characters` list, and
re-shows the menu on anything invalid rather than erroring out or
guessing. One real semantic checked directly rather than assumed before
relying on it: this driver's own `sscanf()` "%d" alone does not require
the entire input to be consumed (confirmed by reading `VM::runSscanf()`
directly, real, faithful LDMud/FluffOS behavior) -- `"5abc"` would still
match `choice = 5` with the trailing text silently ignored, a real LPC
idiom quirk left as-is rather than adding extra validation beyond what
real `sscanf()` itself provides, since it still correctly rejects
non-numeric, zero/negative, and out-of-range input, the actual cases
this menu needs to guard against.

**Two new regression tests** in `test/test_lexer.cpp`
(`testGotLoginPasswordShowsMenuAndLoadsTheChosenCharacter`,
`testGotCharacterSelectionRejectsOutOfRangeAndNonNumericChoices`), plus
the pre-existing login fixture updated to match the new real `login.c`
content (every one of the five pre-existing login/character tests still
passes unchanged, correctly continuing to exercise the single-and-
empty-list fallback paths this item's own new branch does not touch).
Both new tests seed a second character directly via `account_d`'s own
`add_character()`, the same "seed via account_d directly, bypass the UI
for what is not under test" pattern several earlier items' own tests
already established for accounts -- nothing else could give a test
account two characters yet, per this item's own scope note above.

**Live-verified against the real running driver, real bundled
`mudlib/`** (a scratch config on spare port 4165, default dialect, a
real Python TCP client): a real two-character account (`heroaccount`,
seeded via `eval` calling `account_d` directly, the only way to reach
this state today, matching the regression tests' own seeding pattern)
showed the real menu on login; choosing 2 loaded "Lady Nimue", choosing
1 on a separate, later connection loaded "Sir Galahad", each
character's own `login_count` confirmed independent on disk afterward
(2 for Galahad after two separate logins, 1 for Nimue after one -- read
directly from each character's own save file, not inferred); an invalid
choice (99) correctly re-showed the same menu rather than erroring or
disconnecting, and a valid choice afterward still worked normally; a
genuinely single-character account (`seeder_acct`, the throwaway
account used to reach `eval` in the first place) confirmed still going
straight into the game with no menu shown at all, unchanged from before
this item. No errors in the driver's own log across any of it. Scratch
`/accounts` and `/characters` directories and the scratch process both
removed/stopped afterward.

**`notes/ACCOUNT_LOGIN_PLAN.md` updated in place**, matching its own
established per-item update convention: the top status paragraph and
build-ordering item 5 both marked done (bounded) with the full reasoning
above, and an explicit closing note that this plan's own originally
scoped five-item build ordering is now closed out, with the one
deliberately deferred piece ("create a new character from an existing,
logged-in account") named as a real, separate, not-yet-scoped candidate
for a future session, not silently dropped.

**Fresh, honest full-project status sweep, Phase 0 through Phase 3, all
four re-checked directly rather than trusted carried-forward.** `awk`-
counted every row's own checkbox per phase section in `ROADMAP.md`
directly (not eyeballed, not assumed from memory of prior sessions'
own numbers): Phase 0, 16 rows, 16 checked, 0 open (100%, unchanged).
Phase 1, 16 rows total, 10 checked, 6 open (10/11 = 91% on the real-
blockers-only count DGD's own five comparison rows excluded, 10/16 =
63% including them, both unchanged) -- row 1.8 re-read directly, still
`[ ]`, still the same zero-real-corpus-evidence scope already on record
(`#'lfun::`/`#'sefun::`/`#'var::` closure-literal prefixes), nothing new
found. Phase 2, 22 rows, 0 checked, 22 open (0%, unchanged);
every one of `src/jit`, `src/gc`, `src/lsp`, `src/persist`,
`src/security`, `src/proto` confirmed to contain only its own
`instruct.md`, no implementation of coroutines, JIT, hotboot, statedump,
a generational GC, or TLS anywhere in this repository. The one
exception checked and correctly excluded: `src/scheduler/Scheduler.cpp`
is real code, but it predates this sweep by many sessions and backs
Phase 0/1's own already-shipped `call_out()`/`heart_beat()` mechanism,
not any Phase 2 concurrency item -- confirmed by reading what it
actually does, not assumed safe to skip because the directory name
sounded close enough. Phase 3, 8 rows, 0 checked, 8 open (0%,
unchanged). `COMPARISON.md` refreshed to match: its own Phase-numbers
table needed no changes (every number already matched this sweep), but
its stale "715 regression tests" line was bumped to 727 and a dated
re-sweep note appended (in place, not rewritten, matching this file's
own established convention) confirming the fresh check found no drift.

**The recommendation, written here in full rather than left in chat.**
Three real candidates weighed: (A) close row 1.8's own remaining zero-
evidence scope anyway; (B) finish the one piece item 5 above
deliberately deferred, letting an existing, already-logged-in account
create a second character (the real prerequisite for this session's own
character-select menu to ever be reachable by an actual player, not
just by `eval`); (C) begin a dedicated Phase 2 scoping investigation,
the same cold-start-read-the-real-source discipline `privilege_violation()`
and `parse_*` each got before any code was written for either.

**(A) is rejected outright, same reasoning as every prior session that
has weighed it, not re-litigated fresh only because it keeps recurring:**
zero real corpus evidence for `#'lfun::`/`#'sefun::`/`#'var::` anywhere
in `temp/`, still true this session, confirmed by row 1.8's own current
cell text rather than re-derived. Building it now would trade this
project's own standing evidence discipline for a cosmetic checkbox, the
same tradeoff already rejected every time this candidate has come up.

**(B), finishing item 5's own deferred half, is real, immediately
buildable without a fresh scoping session, and closes a genuine
"I built something a player can't actually reach yet" gap this very
session left behind honestly rather than papering over.** It does not
need a cold-start investigation the way (C) does: the shape is already
mostly implied by this session's own work (a new login-flow branch,
reachable from the character menu itself, an "or create a new
character" option alongside the numbered list, calling the same
`got_character_name()`-adjacent validation this session and item 4
already built and tested). The real open question item 5's own writeup
already named and did not answer -- what, if anything, limits how many
characters one account can create -- would need an actual decision
before building, not a large investigation, just a real one.

**(C), a dedicated Phase 2 scoping session, is the recommendation, not
(B), for reasons specific to this project's own sequencing, not a size
judgment.** `ROADMAP.md`'s own stated principle, "Phase 1 before Phase
2," has been satisfied in the narrow sense that mattered for several
sessions now (Phase 1's real corpus-driven work exhausted down to (A)'s
permanently-deferred item and DGD's comparison-only rows), but every
session since that became true has had a concretely-scoped, real,
`ROADMAP.md`-adjacent alternative available first: `parse_*`'s own
`nicks` argument, then `notes/ACCOUNT_LOGIN_PLAN.md` itself, picked
explicitly because Phase 2 needed its own dedicated investigation and
something more immediately buildable existed instead each time. That
plan is now genuinely finished (items 1-5, this session closes the last
one), and (B) is real but comparatively small, a completeness fix for
one feature rather than a new body of work -- the kind of thing worth
doing, but not worth indefinitely postponing Phase 2's own first real
scoping pass behind, the way this project's own history shows happening
four sessions running now. Phase 2 has 22 rows across 5 genuinely
different sub-areas (persistence, concurrency, apply-cache/JIT, efun
breadth, developer experience) with no single obvious next row and,
unlike every Phase 0/1 item, none of it is mudlib-compatibility work
this project's own citation-against-real-source methodology directly
applies to -- it is novel differentiation, not a compatibility gap, so
picking a specific row well enough to build it faithfully needs its own
cold-start investigation first, the same discipline
`privilege_violation()` and `parse_*` each got, not a same-session jump
to code. This session does not attempt that investigation (out of its
own scope, per this session's own instructions to write the
recommendation, not necessarily execute it) -- whoever picks this up
next should treat (C) as the primary recommendation, with (B) as a
legitimate, smaller, immediately-buildable alternative if a session
specifically wants to close out `notes/ACCOUNT_LOGIN_PLAN.md`'s own
last loose end first rather than open Phase 2's own new scoping work.

**2026-08-21 (a further session, continued the same day, yet again
still): the row 1.9 fact-check's own bounded stopgap fix (save_object()/
restore_object() now fail loudly on a width > 1 mapping instead of
silently truncating it), then `notes/ACCOUNT_LOGIN_PLAN.md`'s build
ordering item 4, character creation, bounding items 4-5's combined
scope down to item 4 alone per the plan's own explicit "only after 1-4
work end to end" sequencing. 725 tests, up from 723.**

Oriented fresh per this session's own instructions: read `CLAUDE.md`
(confirmed both non-negotiable rules: `git add` only, no commits/pushes;
no em dashes or emojis).

**The save/restore stopgap.** The prior session's own fact-check found a
real, currently-live silent-truncation bug (not full width-aware
serialization, which remains its own, separately-scoped, larger item,
still open on `ROADMAP.md` row 1.9): `save_object()` on a width > 1
mapping silently wrote only column 0, `restore_object()` always
rebuilt width-1 regardless. Bounded stopgap only, exactly as this
session's own prompt specified: `serializeValue()`'s own `Mapping`
branch (`src/efun/EfunTable.cpp`) now checks `Mapping::width > 1` and
throws a clear, specific error naming `save_object`, the real reason
(LDMud N-column mapping, not yet supported), and pointing at
`ROADMAP.md` row 1.9's own note, before ever writing a byte for that
mapping. Checked inside the recursive writer itself, not only at
`save_object()`'s own top-level per-variable loop, so a width > 1
mapping nested inside an array or another mapping is caught too, not
just one sitting directly in an object variable. No change needed on
the read side (`deserializeValue()`'s own `'M'` case): once the write
side refuses to ever serialize width > 1 data, nothing this driver's
own tab-delimited format can produce still needs detecting there; the
real-FluffOS-format reader/writer pair (`writeRealSaveValue()`/
`parseRealSaveValue()`) is a separate efun (`save_variable()`/
`restore_variable()`), out of this stopgap's own scope (the prompt
named `save_object()`/`restore_object()` specifically), and real
FluffOS mappings have no width concept to begin with, so that pair was
never at risk. Header comment above the `save_object`/`restore_object`
registration also updated with a pointer to the new check, matching
this codebase's own convention of documenting a behavior change at its
canonical read site, not only inline.

Two new regression tests (`test/test_lexer.cpp`):
`testSaveObjectThrowsClearErrorForWidthGreaterThanOneMappingInsteadOfSilentlyTruncating`
(a real width-2 mapping, the `rune-wall.c` shape, `dialect: ldmud`,
`save_object()` throws, message content checked for both "save_object"
and "width") and
`testSaveObjectRestoreObjectStillRoundTripsAWidthOneMappingAfterTheWidthCheck`
(an ordinary width-1 mapping, the common case, saves and restores
correctly, proving zero behavior change for what real corpus content
still actually saves today). The pre-existing
`testSaveObjectRestoreObjectRoundTripsNestedMappingsAndArrays` also
continues to pass unchanged, independent confirmation of the same zero-
behavior-change claim for the common case.

**Live-verified against the real running driver, real bundled
`mudlib/`** (a scratch config on spare port 4155, `dialect: ldmud`, a
real Python TCP client, two temporary scratch probe files under
`mudlib/single/`, removed afterward): a real width-2 mapping literal
(`(["weakness": "fire"; 1])`, the exact `rune-wall.c` shape),
`save_object()`ed inside a real `catch()`, correctly surfaced the new
clear error text rather than a silent wrong result; the attempted save
did leave a real but honestly incomplete file on disk (`"m\t"`, the
variable name and tab written before `serializeValue()` threw, nothing
plausible-looking about it, unlike the pre-fix silent-truncation
failure mode), consistent with this codebase's own existing precedent
for other mid-write throw paths, not a new risk this fix introduces; a
real width-1 mapping in the same live session saved, cleared, and
restored correctly, unaffected. No other errors in the driver's own
log. Scratch files and the scratch process both removed/stopped
afterward.

**Item 4 (character creation, `notes/ACCOUNT_LOGIN_PLAN.md` build
ordering item 4).** Read the plan's own full description first
("name a character, pick whatever minimal starting attributes are
decided on") and bounded it down explicitly, not implicitly: this
bundled mudlib has no attribute/stat/race/class system anywhere (the
wand of creation and one room are its only real content), so inventing
one to attach "starting attributes" to would be speculative game-design
scope creep this plan's own evidence-based discipline does not
support. The one genuinely real, minimal piece was naming the
character itself, since `account_record.c`'s own `characters` array
(built in item 1) already existed to hold it and had never once been
populated. Also bounded items 4 and 5 to just item 4 this session,
matching the plan's own explicit "Only after 1-4 work end to end"
sequencing for item 5, not a size judgment call invented fresh.

Built: a brand-new account, right after `create_account()` succeeds, is
now prompted for its own character's name (`got_character_name()`,
new), validated the same way an account name is (`valid_name()`,
renamed from `valid_account_name()` since the check was always generic,
not account-specific), checked for availability against the character
tree (`account_d.c`'s new `character_name_available()`), then recorded
against the account (`add_character()`, new, appends to
`account_record.c`'s own `characters` array and re-saves the account
record). A returning account's login now reads its own real character
list (`account_d.c`'s new `characters()`) and loads `characters[0]`
(still single-character-per-account, item 5's own scope, not this
one), falling back to the account name itself when that list is empty
-- exactly what every account created before this item still looks
like on disk, so an old account keeps loading its own already-existing
character file correctly.

**One real gap caught live while testing this item, fixed before
calling it done, not left as a known issue.** `add_character()`'s own
first draft only recorded the chosen name in the account's own list,
never reserved the character's own file. Since `/clone/user.c`'s own
`save_character()` (item 3) is the only place a character file actually
gets written, and that only fires on the character's first real
disconnect, a real race existed: a second, unrelated account could
claim the identical name in the window between the first account
choosing it and that first disconnect, since
`character_name_available()`'s own file-existence check would keep
reporting it available the whole time. Caught by this session's own
first regression test for it failing an assertion that should have
trivially passed, not found by inspection first. Fixed by having
`add_character()` also write a real, deliberately empty save file at
the character's own path the moment the name is claimed
(`save_object()` called from inside `account_d.c` itself, which
declares no object variables of its own, so this reserves the name
without needing a throwaway clone) -- the name is reserved immediately,
and a genuinely empty file loads back through `load_character()`'s own
`restore_object()` exactly like a true first login would, no special-
casing needed on the read side.

Four new regression tests
(`testGotCharacterNameRejectsANameAlreadyTakenByAnotherAccount`,
`testExistingAccountLoginLoadsItsOwnChosenCharacterNameNotTheAccountName`,
plus `testLoginAccountCreationFlowEndToEndCreatesRealAccountFile`
extended with character-name assertions), plus every pre-existing login
test's own inline fixtures (`account_d.c`/`login.c`) updated to match
the new real content, and three pre-existing tests' own call sequences
updated to add the new `got_character_name()` step their own new-
account-creation flows now require. Two pre-existing tests needed no
change at all, confirmed rather than assumed: both seed their account
directly via `account_d`'s own `create_account()`, bypassing
`got_character_name()` entirely, so they organically exercise the pre-
item-4 fallback path this item's own backward-compatibility design
relies on, real evidence that fallback works without a dedicated test
written specifically for it.

**Live-verified against the real running driver, real bundled
`mudlib/`** (a scratch config on spare port 4160, default dialect, a
real Python TCP client): a brand-new account (`alice_acct`) choosing a
character name distinct from its own account name (`Nightblade`),
confirmed correct on both the account record (`characters` array) and
the character's own save file; a second, unrelated account
(`bob_acct`) correctly rejected when trying to claim `Nightblade` too,
correctly succeeding with `Shadowfox` instead, `Nightblade`'s own file
confirmed untouched afterward; a returning login for `alice_acct`
correctly loading `Nightblade` again (login count advancing 1 to 2, not
a fresh character silently created under the account name); and a
pre-item-4-style account (seeded directly via `eval`, matching how
every earlier session's own account looks on disk) correctly falling
back to using its own account name as its character name. No errors in
the driver's own log across any of it. Scratch `/accounts` and
`/characters` directories and the scratch process both removed/stopped
afterward.

**`notes/ACCOUNT_LOGIN_PLAN.md` updated in place**, matching its own
established per-item update convention: the top status paragraph and
build-ordering item 4 both marked done with the bounding decision, the
reservation-race fix, and the live verification above all recorded in
full; item 5's own entry updated to note its real prerequisite (item 4)
is now in place, still not started itself.

**Next-session recommendation.** Not re-litigated fresh: the standing
recommendation (continue `notes/ACCOUNT_LOGIN_PLAN.md`'s own build
ordering) still holds, and this session picked up exactly the next
queued item as expected, plus the separately-flagged stopgap fix ahead
of it per this session's own prompt. Build ordering item 5 (multi-
character-per-account, a real character-select menu) remains real,
scoped, and not started -- its own real prerequisite (item 4) is now
done, so whoever picks it up next can build directly on
`account_record.c`'s own already-populated `characters` array and
`account_d.c`'s own `characters()` accessor rather than needing to
build either first. `ROADMAP.md` row 1.9's own still-open, larger item
(full width-aware mapping serialization) remains exactly as open as
this session found it -- the stopgap fix closes the silent-corruption
risk, not the underlying feature gap, and the row's own addendum
already says so.

**2026-08-21 (a further session, continued the same day yet again):
fact-checked two forward-looking architecture concerns from an external
technical review against the real current code (one confirmed real and
already live, not just latent, one confirmed overstated with no real
trigger), then built `notes/ACCOUNT_LOGIN_PLAN.md`'s build ordering
item 3, character persistence, resolving that item's own open design
decision. 721 tests, up from 719.**

Oriented fresh per this session's own instructions: read `CLAUDE.md`
(confirmed both non-negotiable rules: `git add` only, no commits/pushes;
no em dashes or emojis).

**Fact-check 1: does the row 1.9 N-column `Mapping` evolution already
"ripple" into `save_object()`/`restore_object()`/comparison operators,
as an external review warned it eventually would?** Confirmed real and
already present in shipped code today, sharper than this row's own
existing "zero real corpus usage" framing already stated, not merely a
future risk. Read all four real serialization code paths directly
(`src/efun/EfunTable.cpp`): this driver's own writer, `serializeValue()`
(~254-284), and reader, `deserializeValue()`'s `'M'` case (~322-334),
plus the real-FluffOS-on-disk-format writer, `writeRealSaveValue()`
(~505-515), and reader, `parseRealSaveValue()`'s `'['` case (~429-442)
-- all four iterate `Mapping::entries` only and never reference
`Mapping::width`/`extraColumns` (`include/amlp/vm/Value.hpp`) at all.
None throws or warns on a width > 1 mapping: `save_object()` on one
silently writes only column 0, and `restore_object()` always
reconstructs a width-1 `Mapping` regardless of what was actually saved,
a genuine, currently-live, silent data-loss bug for any width > 1
mapping a real mudlib does save (still zero real corpus call sites
today, per this row's own pre-existing finding, so nothing currently
hits it, but the code path itself is already broken, not merely
unbuilt). The comparison-operator half of the same concern does not
apply the way the review frames it: `valuesEqual()`
(`src/vm/Value.cpp:28-55`), the sole backing for LPC's own `==`/`!=` on
any two `Value`s, has no case for `shared_ptr<Mapping>` at all (nor
`shared_ptr<Array>`) for *any* width, falling through to its own final
`return false` -- whole-mapping `==` was never implemented even for the
width-1 mappings that predate this row entirely, a separate, wider,
pre-existing gap the N-column work simply inherited rather than
complicated. Added a dated addendum to `ROADMAP.md` row 1.9's own cell
with the full citation (file:line for all four functions), rather than
leaving this only in chat, matching this project's own standing
discipline against exactly that failure mode.

**Fact-check 2: could deeply nested `unbound_lambda()`/`lambda()`
quoted-code bodies see real variable-resolution performance degrade
during recursive evaluation, as the same review separately warned?**
Confirmed overstated, no real trigger at either implemented or actually
reachable nesting depth. Read `VM::evalQuotedLambdaNode()`/
`VM::callUnboundLambdaBody()` (`src/vm/VM.cpp:985-1043`) directly: a
symbol lookup (a lambda parameter reference) is a linear scan over
`params`, a small, fixed-size vector passed by `const&` unchanged
through every recursive call, never rebuilt, regrown, or accumulated
per frame -- lookup cost is `O(params.size())` on every call regardless
of recursion depth, so the specific degradation mechanism the review
describes does not exist in this implementation at all. The recursion
itself only ever walks the closure's own already-fully-constructed
`lambdaBody` tree (each node visited exactly once, standard depth-first
walk, `O(total nodes)` overall), never a structure that grows during
evaluation the way ordinary recursive LPC function calls can via
runtime data -- nesting depth is fixed at construction time by whatever
literal quoted-code array was authored. Checked real reachability too,
not just the algorithm: `set_driver_hook()` is now implemented (unlike
this mechanism's own older row 1.7/1.8 comment, which is stale on this
point -- a later session built it), and driver-hook firing genuinely
does call through `callClosure()` into this evaluator
(`src/vm/VM.cpp:1131`), so it is reachable end to end today, not
theoretical. The one real corpus source for this exact mechanism,
`temp/core-lib/secure/master/hooks.c`'s own four `unbound_lambda()`
hook bodies (`H_MOVE_OBJECT0`, `H_LOAD_UIDS`, `H_CLONE_UIDS`,
`H_INCLUDE_DIRS`), each nest to depth 1 exactly (a single `({#'closure,
'param, ...})` call, no nested arrays at all) -- read directly, not
assumed. No `ROADMAP.md` note added for this one: the instructions were
to flag a concern only if real and unmitigated, and this one is neither,
so a note would be noise, not evidence; reported here instead, with the
code read as proof.

**Item 3 (character persistence, `notes/ACCOUNT_LOGIN_PLAN.md` build
ordering item 3).** Read the plan's own open design question fresh
before writing anything: whether the character object is a separate,
`account_d`-tracked file, or merges with `/clone/user.c` directly.
Resolved as **merge**: `user.c` itself is the one persisted character
object, calling `save_object()`/`restore_object()` directly on itself,
the same `current_object()`-must-be-the-target reasoning
`account_record.c`'s own header comment already established, and
`user.c` is already a fresh per-connection clone the same way
`account_record.c` is a fresh per-operation clone, so no third "record"
object was needed the way `account_d.c` needed one for itself (a
singleton daemon, not per-account). Matches real corpus precedent too
(`temp/core-lib`'s own `execNewPlayer()`/`execGuestPlayer()` each
resolve straight to one player object, no separate character split),
not just architectural convenience.

New `CHARACTERS_DIR` (`/characters`, `globals.h`), bucketed the same way
as `ACCOUNTS_DIR` but a genuinely separate tree (auth data vs. gameplay
state, a real distinction even though this slice's single-character-
per-account shape means the file names currently match). The one
bucketing rule per tree stays owned by `account_d.c`
(`character_path()`/`ensure_character_dirs()`, new, public since
`user.c`/`login.c` now call them from outside, unlike `account_path()`/
`ensure_dirs()` which stay private) rather than being duplicated in
`user.c`/`login.c` too, the same "one file owns the rule" discipline
`account_record.c`'s own header comment already established for the
account tree.

First real persisted player state, deliberately minimal: `login_count`,
a plain `int` object variable on `user.c`, proving the mechanism end to
end without inventing game mechanics this slice was not scoped to
design. `load_character(path)` (new, called once from `login.c`'s own
`enter_game()`, identically for a brand-new account's first login and a
returning account's Nth -- `restore_object()` on a not-yet-existing
path just leaves every variable at its default, no branch needed) calls
`restore_object()` then increments. A new private `save_character()`
helper persists it, called from **both** real disconnect paths this
mudlib has, not just one: `net_dead()` (already existed, the driver's
own real link-death apply, confirmed live-firing in
`src/net/Server.cpp:357`) and a new `remove()` override
(`command/quit.c`'s own real `"previous_object()->remove()"` path;
`inherit/base.c`'s own `remove()` just destructs with no save at all,
so an explicit "quit" would have silently lost the count without this
override -- matches this file's own pre-existing "bare parent call"
pattern, `id()`'s `base::id(arg)`, rather than reimplementing what
`base::remove()` already does). `login.c` also now shows the restored
count back to the player ("Welcome back! You have logged in N
time(s).") after `load_character()`, a small real touch, not just an
invisible internal counter.

**Regression tests.** Two new tests in `test/test_lexer.cpp`
(`testCharacterLoginCountPersistsAcrossReconnectViaNetDead`,
`testCharacterLoginCountPersistsThroughRemoveNotOnlyNetDead`, covering
each real disconnect path independently rather than assuming one covers
both), plus the four pre-existing login tests' own inline fixtures
updated to match the new real content of `account_d.c`/`login.c`/
`user.c` (all four still pass unchanged otherwise). One real fixture
gap caught while updating them: `ObjectVarHarness::writeFile()` never
creates missing parent directories (matching real `save_object()`'s own
"no missing parent directories either" contract, confirmed already
documented on that efun's own registration comment) and every fixture
in this file before this session used only flat top-level paths, so a
harness needing `/single/*.c`/`/clone/*.c` subdirectories needed
`::mkdir()` calls added to its own setup that no earlier test in this
file needed.

**Live-verified against the real running driver, real bundled
`mudlib/`** (a scratch config on spare port 4150, default dialect, a
real Python TCP client, over a real telnet-negotiated connection): a
brand-new account's first login correctly showed "logged in 1 time(s)",
the real character file landed at the correct bucketed path
(`/characters/t/thistledown.o`) with the correct fields, confirmed by
reading it directly; a second, independent connection's login correctly
restored and showed count 2; a third login followed by an explicit
`quit` command correctly showed count 3 and, confirmed by reading the
on-disk file immediately afterward (before any further connection could
have saved over it), persisted 3 through the `remove()` path
specifically, not incidentally covered by `net_dead()`; a fourth login
correctly continued from there to 4. No errors in the driver's own log
across any of it. Scratch `/accounts` and `/characters` directories and
the scratch process both removed/stopped afterward, confirmed via
`git status` that `mudlib/` shows only the intended tracked changes.

**`notes/ACCOUNT_LOGIN_PLAN.md` updated in place**, matching its own
established per-item update convention: the top status paragraph and
build-ordering item 3 both marked done with the design decision and
reasoning above recorded in full, not left implicit the way the prompt
itself warned against.

**Next-session recommendation.** Not re-litigated fresh this session:
the standing recommendation (continue `notes/ACCOUNT_LOGIN_PLAN.md`'s
own build ordering) still holds, and this session picked up exactly the
next queued item, item 3, as expected. Build ordering items 4 and 5
(character creation flow for brand-new accounts, then multi-character
selection once 1-4 work end to end) remain real, scoped, and not
started -- item 4 in particular now has a real, working persistence
layer under it (this session's own item 3) to build on, so whoever
picks it up next does not need to re-derive that part first. The two
fact-checked review concerns above are closed for now (one flagged for
real in `ROADMAP.md`, one reported and set aside as not real); neither
blocks or changes this plan's own next item.

**2026-08-21 (a further session, continued the same day again): resolved
a loose end flagged (not fixed) by the prior session, a stale citation
to a nonexistent `secure/daemon/account_d.c` found in three files, not
two -- then built `notes/ACCOUNT_LOGIN_PLAN.md`'s build ordering item 2,
real login integration on top of item 1's `account_d.c`/
`account_record.c`. `/clone/login.c` reworked into a real `input_to()`
account-name-then-password state machine wired to `ACCOUNT_D`,
replacing the previous unconditional clone-and-`exec()` with no auth at
all. Four new regression tests added; live-verified against the real
running driver and the real bundled `mudlib/` tree over a real
connection (new account, correct second-connection login, wrong-
password rejection through the real retry limit). 719 tests passing, up
from 715.**

Oriented fresh per this session's own instructions: read `CLAUDE.md`
(confirmed both non-negotiable rules: `git add` only, no commits/pushes;
no em dashes or emojis).

**The dead-citation resolution.** The prior session's own entry below
named "two other files" citing a nonexistent `secure/daemon/
account_d.c`, found while researching real reference material for the
account-storage slice, flagged but explicitly not fixed. Grepped fresh
rather than trusting that count: the real total is **three files**, not
two, seven occurrences (`src/efun/EfunTable.cpp`, two separate citation
sites, plus a third mention inside the `save_object`/`restore_object`
registration comment; `src/vm/VM.cpp`, one site, entirely missed by the
prior session's own summary; `test/test_lexer.cpp`, three sites, not
two). Investigated what the citation actually was before deciding how
to resolve it, rather than guessing: it is not fabricated and not real
vendored corpus content either. `git log --all --diff-filter=D -- 'mudlib/
secure/*'` returns nothing (that path was never tracked in this repo's
own history at all), and it does not exist in any of the six vendored
corpora under `temp/` (already confirmed by the prior session). But
`STATUS-ARCHIVE.md`'s own much earlier dated entries (the closures/
`ObjectFrameGuard`/`CallParent` recon sessions) describe a real, live,
extensive walkthrough against it: a full account-creation flow driven
over a real socket connection, a real save file inspected on disk, real
bugs found and fixed because of it. Cross-referencing the file paths
those same entries cite alongside it (`secure/std/login.c`,
`secure/daemon/chat.c`/`events.c`/`finger.c`/`bank.c`, `daemon/
intermud.c`, `daemon/services/who.c`/`auth.c`, `domains/Praxis/*`)
against every corpus under `temp/` found an exact structural match:
`temp/nightmare3` has every one of those paths for real, except
`account_d.c` and `wiztools.c` specifically, which do not exist there
either. Conclusion, not previously on record anywhere: this project's
own early history ran a scratch mudlib built on a nightmare3-derived
skeleton with some genuinely original files of its own layered on top
(`account_d.c` among them), used once for live driver verification and
then discarded like every other scratch object this project's own
methodology produces, never committed to this repo, and not preserved
in any vendored corpus either. The citing comments' own account of what
they surfaced (the `sizeof()` empty-string idiom, the three-file
`sprintf()` shape survey, the `CallParent` opcode's `::create()` gap,
the `ObjectFrameGuard`/closure-owner bug, the `unguarded()`/`security.c`/
`master.c` three-hop chain) is real project history, not invented, so
deleting it outright would lose real information the "citing code
itself is unreachable" removal option does not apply to anyway (every
one of those mechanisms, `sizeof()`, `sprintf()`, `CallParent`,
`ObjectFrameGuard`, closures, is real, load-bearing, still-shipping
driver code today). Resolved instead by correcting each citation in
place: every mention of `secure/daemon/account_d.c` across all three
files now carries an explicit note that it is a since-discarded early
scratch mudlib object, not real vendored corpus content and not this
session's own real, shipped `/single/account_d.c`, with a cross-
reference to the real file and (where the original claim no longer
holds, e.g. the real file uses `name == ""` rather than `!sizeof(name)`,
calls `save_object()` directly with no `unguarded()` hop, has no
`::create()` at all since it inherits nothing) an explicit note of the
difference rather than a silent implication of identity. Rebuilt clean
after every edit (`src/efun/EfunTable.cpp`, `src/vm/VM.cpp`,
`test/test_lexer.cpp` all comment-only changes, zero behavior change,
confirmed by a full rebuild and the unchanged 715-test baseline before
moving on to new work).

**Login integration (`notes/ACCOUNT_LOGIN_PLAN.md` build ordering item
2).** Read the plan document's own scope fresh rather than reinventing
it: item 2 is already exactly sized to one session ("account name ->
password, no character concept yet"), so no further bounding was
needed the way the prompt's own fallback instruction anticipated might
be necessary. Read the current `mudlib/clone/login.c` (`// needs fixed
to handle passwords`, its own top-of-file comment, clones `/clone/user`
unconditionally with zero auth), `mudlib/clone/user.c`, and
`mudlib/single/master.c`'s `connect()` before changing anything.

Reworked `/clone/login.c` into a real `input_to()` state machine:
`logon()` prompts for an account name -> `got_account_name()` branches
on `ACCOUNT_D->account_exists()` -> an existing account goes to
`got_login_password()` (`INPUT_NOECHO`), a new one goes to
`got_new_password()` then `got_confirm_password()` (both `INPUT_NOECHO`,
a real confirm-password step, matching `temp/core-lib`'s own
`setPassword`/`confirmPassword` pair already cited in the plan). Success
either way reaches `enter_game()`, which does exactly what the old
`logon()` did (clone `/clone/user`, `set_name()`, `exec()`, `setup()`,
`move(START_LOC)`, `destruct(this_object())`), now gated on a real
account instead of running unconditionally, and using the account name
itself as the character name -- the same single-character-per-account
shape this project's own prior scratch-mudlib live verification already
exercised for real (`STATUS-ARCHIVE.md`'s "Confirm `<name>` as your
account and first character name?" walkthrough), reused rather than
invented fresh. A login-attempt counter (`MAX_LOGIN_TRIES`, 3,
disconnects after) and an idle `call_out()` timeout (`LOGIN_TIMEOUT_SECS`,
90, matching `temp/core-lib`'s own cited `call_out("timeout", 90)`) are
both in from this first pass, per the plan's own "should be part of
this from the start, not bolted on later" note. `MIN_PASSWORD_LEN` (5)
reuses this project's own prior live-verification session's real prompt
text against the old scratch mudlib ("Please choose a password of at
least 5 letters"). `INPUT_NOECHO` (real comm.h `I_NOECHO`, `0x1`) is a
new `globals.h` constant -- this mudlib had never had a header-level
name for it before, only the driver-side citation. Deliberately not
built this slice, matching the plan's own item 2 scope exactly rather
than reaching into item 3's territory: the character object design
decision, and the y/n account-name confirm step the plan's own fuller
"Proposed architecture" section describes (flagged as a deliberate
simplification in `login.c`'s own header comment, not a silently
smaller scope).

**Regression tests.** Four new tests added to `test/test_lexer.cpp`
(`testLoginAccountCreationFlowEndToEndCreatesRealAccountFile`,
`testLoginExistingAccountCorrectPasswordOnASecondConnectionSucceeds`,
`testLoginWrongPasswordRejectedAndDisconnectsAfterMaxLoginTries`,
`testLoginInvalidAccountNameWithSlashReprompts`), using the same
`ObjectVarHarness` + `socketpair` + `Connection` + `OutputContext`
pattern this suite's own pre-existing "connect/input protocol" tests
already established (the prior session's own "no unit tests were
added" choice for the account-storage slice was specific to that slice,
per its own stated reasoning, real bundled mudlib content gets live-
verification instead of unit tests, not a standing rule against ever
testing mudlib content this way; this session's own prompt asked for
tests specifically, so this is that, not a reversal). The real current
content of `account_d.c`/`account_record.c`/`login.c` is written into
each test's harness as inline fixtures (kept in sync by hand if any of
the three is edited later, the same tradeoff every other inline fixture
in this file already makes rather than reading real files off disk at
test time, which would make the whole suite fragile to its own working
directory the way nothing else in it currently is); `/clone/user.c` and
`/single/start_room.c` are deliberately NOT real copies, minimal stand-
ins for what `enter_game()` needs and nothing this session's own work
touches. One real bug caught and fixed while writing these: the first
draft called `Connection::takePendingInputTo()` (destructive, consumes
the registration) purely to *inspect* which handler had been
registered, then separately called `Server::dispatchLine()` expecting
it to find and route to that same registration -- already consumed by
the inspection, so the second step silently no-opped instead of
advancing the state machine, caught by the test's own first real
assertion failure rather than passing on a false premise. Fixed by
driving every step directly through the extracted handler name instead
of round-tripping through `dispatchLine()` a second time (that
mechanism, "does `dispatchLine()` find and call a pending handler," is
already this same file's own separate, pre-existing test,
`testDispatchLinePrefersPendingInputToHandlerOverProcessInput`, not
something these new tests needed to re-prove).

**Live verification against the real running driver, real bundled
`mudlib/`** (a scratch config on spare port 4146, default dialect, a
real Python TCP client, over a real telnet-negotiated connection, not
`eval` calls this time since the thing under test is the connection-
level state machine itself): a brand-new account (`aetherwalker`)
walked through name -> new-account branch -> password -> confirm
password -> real `crypt()`-hashed account file landing on disk at the
correct bucketed path (`/accounts/a/aetherwalker.o`) -> straight into
the one real room, confirmed both via the driver's own output and by
reading the account file's contents directly; a second, independent
connection to the same account -> correctly recognized as existing ->
password prompt (real `INPUT_NOECHO` telnet suppression confirmed in
the raw byte stream) -> correct password -> straight into the game;
a third connection given three wrong passwords in a row -> correctly
rejected each time with a retry count -> real disconnect after the
third, matching `MAX_LOGIN_TRIES`; a fourth connection given a name
containing `/` -> correctly rejected and re-prompted rather than either
crashing or being treated as a literal (nonexistent) account. No
errors in the driver's own log across any of it. Scratch `/accounts`
directory and scratch process both removed/stopped afterward, confirmed
via `git status` that `mudlib/` shows only the intended tracked changes.

**`notes/ACCOUNT_LOGIN_PLAN.md` updated in place**, matching its own
established per-item update convention: the top status paragraph and
build-ordering item 2 both marked done with the reasoning above,
item 2's own scope note preserved verbatim as the record of what was
deliberately not built this slice.

**Next-session recommendation.** Not re-litigated fresh this session
(the prior session's own three-way comparison, (A) close Phase 1's
zero-evidence items, (B) `notes/ACCOUNT_LOGIN_PLAN.md`, (C) begin real
Phase 2 scoping, still stands and is written in full below in this same
file's immediately following entry): this session picked up (B)'s own
next queued item as that comparison's own logic already implied it
would, and (B) is not yet exhausted. `notes/ACCOUNT_LOGIN_PLAN.md`'s
own build ordering items 3 through 5 (the character object design
decision, character creation, multi-character selection) remain real,
scoped, and not started -- item 3 in particular needs its own design
decision made (persisted-subclass-of-`user.c` vs. merged single
object), not a default assumed, before any code gets written, matching
the plan document's own explicit "deliberately does not pick one yet."
Whoever continues this next should start there, or re-run (A)/(C)'s own
evidence check fresh if meaningful time has passed since this was
written, per this project's own standing discipline against trusting a
prior session's snapshot blindly.

**2026-08-21 (a further session, continued the same day): a fresh
full-project status sweep (Phase 0 confirmed still 16/16, Phase 1
confirmed still 10/11 with the full remaining item list re-verified,
Phase 2/3 confirmed still 0/22 and 0/8), `COMPARISON.md` refreshed to
match, and this session's own next-priority recommendation written
directly into this entry, not left in chat only, closing a real gap
this project has now hit twice (see below). No test count change this
session (715, unchanged, no driver code touched).**

Oriented fresh per this session's own instructions: read `CLAUDE.md`
(confirmed both non-negotiable rules: `git add` only, no commits/pushes;
no em dashes or emojis).

**Why this entry is written the way it is.** The user asked directly
for the recommendation below to be written into this file in enough
detail that a future session or a different reviewer could read it cold
and understand the choice without the original conversation, because
the immediately prior session's own equivalent recommendation was given
only in the chat reply and turned out to be unrecoverable afterward.
That is not a one-off: this project has hit the exact same failure mode
before, on record in this very file, `STATUS.md`'s own 2026-08-19 entry
for the `parse_*`-versus-Phase-2 decision ends with "Full reasoning
given directly to the user this turn, not duplicated here: see this
same session's own reply for the three-way comparison." That reasoning
is also gone now, for the same reason. This entry is written to not
repeat that mistake a third time.

**Phase 0: confirmed still 16 of 16 rows checked, 100%, no open
sub-gap.** Re-read every row's own checkbox directly rather than
trusted from memory: all 16 rows (`0.1` through `0.15`, including
`0.13a`) read `[x]`, and `0.13a`'s own cell text confirms no remaining
sub-gap ("Nicks implemented, 2026-08-20", the last item this row had on
record). Unchanged from the last status-read pass.

**Phase 1: confirmed still 10 of 11 real (non-DGD) rows closed, 91%,
the fraction itself unchanged by this session's own driver work.** Row
1.7 was already checkbox-closed before this session (it was one of the
already-counted 10, "partial" status, real evidence-backed items still
open inside its own cell); this session's `call_out_info()`/`input_to()`
work closed the last of those items, but did not move the row-level
fraction, since the row was already counted as closed at the row
granularity `ROADMAP.md`'s own table tracks. Row 1.8 remains the only
row genuinely still open. Exact remaining scope, read directly from
each row's own current cell text rather than summarized from memory:

- **Row 1.8, `#'lfun::name`/`#'sefun::name`** (more forced-tier closure
  prefixes, reusable on the same mechanism `#'efun::` already
  established) **and `#'var::variable_name`** (a structurally distinct
  closure kind, real `CLOSURE_IDENTIFIER`, a reference to a global
  variable, not a callable at all, `doc/LPC/closures:43`,
  `closure.c:450`/`524`/`977`/`1198`/`4178`). Zero real mudlib call
  sites for any of the three across every corpus vendored in `temp/`,
  re-confirmed fresh by this row's own investigating session
  (2026-08-20); the only hit anywhere is the LDMud driver's own
  changelog prose noting when it added them.
- **Row 1.7's own remaining sub-items** (the row itself is closed,
  these are named exceptions inside it): `H_LOAD_UIDS`/`H_CLONE_UIDS`/
  `H_INCLUDE_DIRS` driver-hook trigger points (3 real call sites, all
  in one file, `secure/master/hooks.c`); real per-hook type-map
  validation (`hook_type_map[]`); plain dialect-agnostic `lambda()`;
  `inaugurate_master()`'s own arg=1/2/3 master-reload/reactivation
  cases (only arg=0, first boot, is wired); the remaining plain-string
  `hooks.c` hooks (`H_CREATE_SUPER`/`H_CREATE_OB`/`H_CREATE_CLONE`/
  `H_MODIFY_COMMAND_FNAME`/`H_NOTIFY_FAIL`/`H_TELNET_NEG`/
  `H_AUTO_INCLUDE`); and roughly 20 of the 26 real doc-cataloged
  `privilege_violation()` operations still ungated (several correspond
  to packages this driver does not implement at all, mysql/pgsql/
  sqlite, the erq demon, wizlist, so gating them would be meaningless
  until those packages exist; the rest have no real corpus evidence
  beyond `core-lib`'s own generic default-case fallback). All zero real
  corpus pressure, deferred on the same evidence discipline as
  everywhere else in this row.
- **Row 1.9's own remaining sub-items** (row itself closed): the
  `m_allocate`/`m_entry`/`m_reallocate`/`m_add`/`m_contains` N-columns-
  wide efun family and the `([:width])` empty-mapping literal, zero
  real call sites across every corpus in `temp/`.
- **DGD's own five still-open rows (1.11-1.15)**: real, scoped,
  cited against `temp/dgd/`'s own source, but comparison context, not a
  Phase 1 blocker, per this project's own explicit goal (a FluffOS/
  LDMud-level driver done better than either, not three-way DGD
  parity).

**Phase 2/3: confirmed still 0/22 and 0/8, planning documents only.**
Every row in both phases reads `[ ]`. Checked `src/jit`, `src/gc`,
`src/lsp`, `src/persist`, `src/security`, `src/scheduler`, `src/proto`:
each contains its own `instruct.md` and nothing else, no implementation
of coroutines, JIT, hotboot, statedump, a generational GC, TLS, or any
other Phase 2/3 item exists anywhere in this repository.

**`COMPARISON.md` refreshed to match**, in place, the same "refreshed
in place, not narrated" convention this file already used rather than
appending new prose sections: the Phase 0 summary no longer claims a
remaining `parse_*` sub-gap (closed last session); the row 1.7 bullet
describing `privilege_violation()` now states its real, current
four-trigger-point scope instead of framing it as an unbuilt future
candidate; the "Driver hooks" feature-table row now reads 5 real
trigger points wired (`H_MOVE_OBJECT0/1`, `H_MODIFY_COMMAND`, `H_RESET`,
`H_CLEAN_UP`), up from 2; a new feature-table row was added for
`privilege_violation()` itself (4 of 26 real operations gated); the
"what AMLP does not have" bullet's hook-trigger-point count updated to
match; and the closing test-count line bumped from 694 to 715. The
Phase/rows/done/open/percent table itself needed no change, both real
fractions (Phase 0 100%, Phase 1 real-blockers-only 91%) were already
correct. `ROADMAP.md`'s own Phase 1 status-read header gained a dated
"Update, 2026-08-21" paragraph closing the loop on its own prior
`privilege_violation()` framing, rather than being rewritten in place,
matching this file's own established per-row update convention.

**The recommendation, written here in full rather than left in chat.**
Three real candidates were weighed, the same three named in this
session's own prompt: (A) continue closing Phase 1's remaining
low-corpus-usage items anyway, for completeness; (B) pick up
`notes/ACCOUNT_LOGIN_PLAN.md`, now that Phase 1's well-evidenced work
has thinned out; (C) begin real Phase 2 planning-to-code work (JIT,
hotboot, a real GC, statedump, the LSP server).

**(A), continuing to close Phase 1's zero-evidence items anyway, is
rejected outright, not merely deprioritized.** Every one of the items
listed above under Phase 1's own remaining scope was already deferred
on an explicit, repeatedly-applied project discipline: real corpus
evidence decides what gets built, not checkbox completeness for its
own sake (the same discipline that correctly rejected a `bind_lambda()`
stand-in, deferred plain `lambda()`, and deferred DGD's own five rows).
Building `#'var::`, a structurally distinct closure kind needing its
own new value-representation work, for zero confirmed real callers
anywhere in `temp/`, or gating 20 more `privilege_violation()`
operations, several for packages this driver does not even implement,
would directly reverse that discipline for the sake of a cosmetic 11/11
rather than real compatibility value. Real evidence does not support
this candidate; it is not a live option unless new corpus evidence
surfaces.

**(C), beginning real Phase 2 planning-to-code work, is real and not
blocked, but is the weaker pick this session, for reasons specific to
how this project has made every other build decision, not a
size-based preference.** `ROADMAP.md`'s own stated sequencing
principle is "Phase 1 before Phase 2. Dialect abstraction unlocks
concurrent dialect work" (Phase 2's own header). Phase 1 being 10/11
with the 11th deliberately, permanently deferred pending evidence that
does not currently exist arguably satisfies that principle's actual
intent (a stable dialect abstraction to build on, which already
exists: `BootApi`, dialect-gated efuns throughout `EfunTable.cpp`), so
Phase 1's own incompleteness is not by itself a hard block. The real
reason to not pick this now: every other "big" item this project has
built (`parse_*`, `valid_read`/`valid_write`, `privilege_violation()`
itself) got its own dedicated cold-start scoping session, real source
read in full, real corpus usage checked, before any code was written,
specifically because guessing at scope for something this size wastes
a session or worse, produces an unfaithful shim. Phase 2 has 22 rows
across 5 genuinely different sub-areas (persistence, concurrency,
apply-cache/JIT, efun breadth, developer experience) with no single
obvious next row, and, unlike every Phase 0/1 item, none of it is
mudlib-compatibility work this project's own citation-against-real-
source methodology directly applies to: Phase 2 is novel
differentiation, not a compatibility gap, so the evidence bar that
decided every prior priority call (corpus call-site counts) does not
transfer cleanly. Picking a specific Phase 2 row well enough to build
it faithfully this session, rather than speculatively, would need its
own scoping investigation first, the same discipline `privilege_violation()`
got two sessions ago, not a same-session jump straight to code.

**(B), picking up `notes/ACCOUNT_LOGIN_PLAN.md`, is the recommendation,
on real evidence, not just because A is rejected and C is deferred.**
Four concrete reasons: first, it is the only candidate that is already
fully scoped and immediately buildable right now, not something
needing its own investigation session first, its own document already
did that work (2026-08-19): every efun it needs (`crypt`, `save_object`/
`restore_object`, `input_to`, `exec`, `mkdir`/`get_dir`/`file_size`,
`valid_read`/`valid_write`) is confirmed real and working today, a real
reference implementation shape was already read directly from
`temp/core-lib/secure/login.c`'s own real `input_to()`-driven state
machine, and a concrete first build slice is already named (`/single/
account_d.c`: account file format, `create_account`, `check_password`,
`account_exists`, testable in isolation via `eval`, no login
integration yet). Second, the document's own explicit self-deferral,
"does not compete with, block, or get worked ahead of the current
Phase 0/1 driver priority", was written when Phase 0 was still open and
Phase 1 still had real, well-evidenced driver-side work in flight
(`parse_*`, then `privilege_violation()`'s first two trigger points);
that condition has now genuinely lapsed, Phase 0 is 100% and Phase 1's
real corpus-driven work is exhausted down to (A)'s explicitly-rejected
items and (C)'s DGD-only rows, so picking this up now honors the
document's own stated condition rather than jumping the queue. Third,
it directly grows the bundled mudlib past its current "one room, a
wand, no real login" state into something a real player could actually
use, a different, concrete kind of value than either A (a cosmetic
percentage) or C (architecture nobody outside this project can observe
yet). Fourth, choosing it does not foreclose C: Phase 2 stays exactly
as real and as open as it is today, ready for its own dedicated
scoping session whenever it is picked up next, nothing about building
mudlib content this session makes that scoping work any smaller or
larger later.

**Built this same session, after the recommendation above: build
ordering item 1 from `notes/ACCOUNT_LOGIN_PLAN.md`, real account
storage.** New `/single/account_record.c` (a small per-account
data-holder object, `name`/`hash`/`created`/`characters` variables,
cloned fresh per operation and destructed right after: real
`save_object()`/`restore_object()` always act on `current_object()`'s
own variables, no target-object argument, so a per-account on-disk
file needs a per-account object to be `current_object()` while the
efun runs, not the daemon calling it from outside). New
`/single/account_d.c`: `account_exists()`, `create_account()`,
`check_password()`, files bucketed by the account name's own first
letter under a new `ACCOUNTS_DIR` (`/accounts`), the exact same
`name[0..0]` idiom this mudlib's own pre-existing `simul_efun.c:55`
already uses for `user_path()`, confirmed by reading that file rather
than invented fresh. `crypt(password, 0)` hashes a new password (real
salt-generation idiom, already cited in `crypt()`'s own EfunTable.cpp
registration comment); `crypt(password, existingHash) == existingHash`
verifies one (passing an already-computed hash back in as the "salt"
argument re-derives it with the same embedded salt, confirmed directly
from this driver's own `crypt()` implementation, a string salt of
length >= 2 is used as-is, not regenerated). Two new `globals.h`
constants, `ACCOUNT_D`/`ACCOUNT_RECORD`/`ACCOUNTS_DIR`.

**One tangential finding surfaced while researching real reference
material for this, flagged rather than silently passed over.**
`src/efun/EfunTable.cpp`'s own `save_object`/`restore_object`
registration comment, and two comments in `test/test_lexer.cpp`
(`testBareParentCallInvokesInheritedFunctionNotLocalOverride`'s own
header and one other), cite a file at `secure/daemon/account_d.c` as
something "found live compiling" with "confirmed live" behavior
against it. Searched for it directly before trusting the citation, the
same discipline used everywhere else in this project: it does not
exist anywhere in any vendored corpus, extracted or zipped
(`temp/core-lib`, every other extracted tree, and every zip/tar
archive under `temp/`, searched by name). The closest real match,
`temp/lima/lib/daemons/account_d.c`, is a same-named but unrelated
in-game banking/currency daemon (`query_account`/`deposit`/`withdraw`,
gold and credit balances), not a login/account-auth file at all, read
in full before ruling it out rather than assumed from the filename
alone, the same false-positive-by-name trap `notes/ACCOUNT_LOGIN_PLAN.md`
had already separately flagged for `skylib_fluffos_v3`'s own
`bank_accounts/`. This session's own new `/single/account_d.c` is
therefore original design work against this plan's own real citations
(`temp/core-lib/secure/login.c`'s state-machine shape, the real
`crypt()`/`save_object()` semantics already confirmed elsewhere), not
a port of the phantom-cited file, and does not reuse its path or
naming. The stale citation itself was not corrected this session,
tangential to this session's own actual task and not investigated
further than confirming it does not point at anything real: flagged
here so a future session does not build on it as if it were a
confirmed real source the way every other citation in this codebase is
meant to be.

**Live-verified against the real running driver, real bundled
`mudlib/`** (a scratch config on spare port 4144, default dialect, a
real Python TCP client, real `eval` calls only, no scratch objects or
master edits needed this time): `account_exists("bob")` correctly `0`
before creation; `create_account("bob", "hunter2")` returns `1`, the
real on-disk file (`mudlib/accounts/b/bob.o`) inspected directly,
correct bucketed path and correct saved fields; `account_exists("bob")`
now `1`; `check_password("bob", "hunter2")` returns `1`,
`check_password("bob", "wrongpass")` returns `0`; a duplicate
`create_account("bob", ...)` correctly returns `0`, does not overwrite;
case-insensitivity confirmed (`account_exists("BOB")`/
`check_password("BOB", "hunter2")` both resolve to the same account); a
second account (`"Alice"`, a different bucket letter) created
independently and correctly; empty name and empty password both
correctly rejected (`0`, no file written). Scratch accounts directory
removed afterward, confirmed via `git status` that `mudlib/` shows only
the two genuinely new files plus the `globals.h` addition.

**No new C++ regression tests for this slice, a deliberate choice, not
an oversight, stated so it reads as one on a future review.** Checked
first: every one of this suite's roughly 715 tests that touches mudlib
content writes its own scratch temp mudlib (`ObjectVarHarness`'s own
`mkdtemp()`-based tempdir), none exercises the real bundled `mudlib/`
tree directly, confirmed by grep, zero hits for a config pointing at
the real `mudlib_root: mudlib` anywhere in `test_lexer.cpp`. This
project's own established split, confirmed by precedent rather than
decided fresh here: driver-level C++ mechanisms (efuns, VM behavior,
dialect gates) get unit tests in this suite; real bundled mudlib
content (`master.c`/`simul_efun.c`/`wand_of_creation.c`/`login.c`
before it) gets live-running-driver verification instead, documented
in `STATUS.md`'s own dated entries, the same pattern this entry follows
above. `notes/ACCOUNT_LOGIN_PLAN.md` updated in place to record this
slice done and the reasoning above, matching its own established
per-item update convention.

715 tests passing (unchanged: no driver-side `src/` code was touched
this session at all, only documentation and new mudlib content).

**2026-08-21 (a further session): docs cleanup (personal scoping/plan
notes relocated to an untracked `notes/` folder, the `" -- "` em-dash
stand-in rewritten to proper punctuation across every active doc), then
row 1.7's own remaining `call_out_info()`/`input_to()` privilege_violation()
follow-on closed for real (715 tests, up from 709).**

Oriented fresh per this session's own instructions: read `CLAUDE.md`
(confirmed both non-negotiable rules: `git add` only, no commits/pushes;
no em dashes or emojis).

**The prior session's own next-priority recommendation could not be
recovered, said so directly rather than guessing.** Checked `git log`
first: the privilege_violation() cold-start investigation and its first
real slice (`bind_lambda()`/`set_driver_hook()`, commit `81f849b`) were
already committed, so nothing from that prompt's Steps 1 to 3 needed
redoing. But the specific three-way comparison the user asked to recover
(`call_out_info()`/`input_to()` vs. `notes/ACCOUNT_LOGIN_PLAN.md` vs.
starting Phase 2 planning) is not in `STATUS.md`, not in `ROADMAP.md`,
and this session has no access to that prior session's own raw chat
transcript, only what got persisted to repo files. This matches a known
pattern already on record elsewhere in this project (row 0.13a's own
"Full reasoning given directly to the user this turn, not duplicated
here" note, `STATUS.md`'s own 2026-08-19 entries): some sessions give
their full reasoning only in the chat reply, not in any file. Reported
this plainly rather than fabricating a recollection, then independently
re-derived a fresh recommendation from the actual current evidence
(below), rather than assume the missing one would have agreed.

**`notes/ACCOUNT_LOGIN_PLAN.md`'s relocation was pure repo hygiene, not
a priority signal, stated plainly since the user asked directly.** The
prior session's move of all three `*_PLAN.md`/`*_SCOPING.md` files out
of the tracked repo (per the user's own explicit "they're for me not the
public repo" instruction) was applied uniformly by filename pattern, not
by reading or judging any file's content or priority. The file's own
text already says this itself: "does not compete with, block, or get
worked ahead of the current Phase 0/1 driver priority." Nothing about
moving it to `notes/` changes that standing; it is still the live scope
document for whenever mudlib account/login work is actually picked up,
exactly as before the move.

**Fresh three-way comparison, evidence-based.** `call_out_info()`/
`input_to()`: a real, already-cited, already-scoped follow-on sitting in
row 1.7's own cell (`call_out.c:805-829`, `comm.c:7315-7317`), the last
concretely-open item keeping Phase 1 at 10/11 rather than 11/11, sized
for one session, no new architecture needed (reuses the exact
`VM::privilegeViolation()` shared core `bind_lambda()`/`set_driver_hook()`
already built). `notes/ACCOUNT_LOGIN_PLAN.md`: real, well-scoped mudlib
work, explicitly self-subordinated in its own text to "the current Phase
0/1 driver priority", not blocked, just not what its own author flagged
as next. Phase 2: `ROADMAP.md`'s own stated sequencing principle is
"Phase 1 before Phase 2. Dialect abstraction unlocks concurrent dialect
work" (see Phase 2's own header), and Phase 1 still has this one real,
concretely-scoped item open, so starting speculative Phase 2 work now
would violate the project's own already-stated ordering, not just be a
matter of taste. `call_out_info()`/`input_to()` picked on this basis:
smallest, most concretely scoped, continues the project's own stated
priority order, and does not preempt the account/login plan's own
explicit self-deferral.

**Docs cleanup, this session, before the driver work.** Moved
`mudlib/{WAND_OF_CREATION_SCOPING,LIBRARY_MUDLIB_PLAN,ACCOUNT_LOGIN_PLAN}.md`
to a new `notes/` folder, `git rm --cached`'d them, added `notes/` to
`.gitignore` (untracked, still on disk). Wrote a small heuristic script
to rewrite every `" -- "` em-dash stand-in (CLAUDE.md's own rule says
"a period, comma, or start a new sentence instead", not a literal
double-hyphen, which reads exactly like the character it was meant to
avoid) into a paired comma, a conjunction-led comma, or a colon
depending on context: paired dashes bracketing an aside became paired
commas, a clause led by a conjunction (so/since/because/...) got a
comma, everything else (the dominant "citation/justification" pattern
throughout this corpus) became a colon. Ran on `STATUS.md` first,
hand-verified all 62 changes via diff, found and fixed one double-colon
collision, then applied the corrected script to `ROADMAP.md` (426),
`COMPARISON.md` (70), `INSTALL.md` (14), `CREDITS.md` (5), and every
`src/*/instruct.md` file with a hit (129 total), spot-checking dozens
more across `ROADMAP.md`. Zero `" -- "` remain in any of them.
`STATUS-ARCHIVE.md` (838 instances, a dated historical log) and source/
test code comments (~2,150 instances, real risk of colliding with an
actual C++ `--` decrement operator) stayed out of scope this pass, per
the user's own explicit choice. Rebuilt and reran the full suite after:
709 tests passing, zero regressions (docs-only).

**`call_out_info()` gated dialect-aware.** Real LDMud's own
`f_call_out_info()` (`call_out.c:805-829`) wraps its own
`get_all_call_outs()` in `privilege_violation(STR_CALL_OUT_INFO,
&const0, sp)`, degrading to the real empty array on denial. Real
FluffOS's own `f_call_out_info()` (`efuns_main.c:292`ff) has no such
gate at all, confirmed directly (FluffOS never had a
`privilege_violation()` mechanism at all). This driver's own
`call_out_info()` already ported the real FluffOS shape unconditionally,
so the fix is dialect-branched (`vm.config().dialect() == "ldmud"`),
not unconditional the way `bind_lambda()`/`set_driver_hook()` could be:
only under `dialect: ldmud` is the new gate consulted at all; under
FluffOS it stays exactly as before.

**`input_to()` gated on the real `INPUT_IGNORE_BANG` flag bit
specifically, not dialect.** Real `comm.c:7315-7317`'s own `"(flags &
IGNORE_BANG) && !privilege_violation4(STR_INPUT_TO,
svalue_object(command_giver), 0, flags, sp)"`, resolved via real
`privilege_violation4()`'s own "whom && !how_str" branch
(`interpret.c:8578-8621`) to `master->privilege_violation("input_to",
current_object, command_giver, flags)`. Real `INPUT_IGNORE_BANG` is bit
128 (`mudlib/sys/input_to.h`). Confirmed real FluffOS's own `input_to()`
flag bits (`I_NOECHO`=0x1, `I_NOESC`=0x2, `I_SINGLE_CHAR`=0x4, get_char
only, `comm.h`) never define that bit at all, so gating on it
unconditionally, regardless of dialect, carries the same "no FluffOS
equivalent, no conflict risk" safety already established for
`bind_lambda()`/`set_driver_hook()`, confirmed by grep before relying on
it rather than assumed.

**6 new regression tests** (`test_lexer.cpp`): `call_out_info()` denied
and granted under `dialect: ldmud`, plus a FluffOS-dialect regression
proving it stays fully ungated even against an actively denying master;
`input_to()` denied and granted with the bang flag set, plus a control
test proving a flags value that omits the bit never consults
`privilege_violation()` at all even against a denying master.

**Verified live against the real running driver, real bundled
`mudlib/`** (two scratch configs on spare ports 4141/4142, one
`dialect: ldmud` one default FluffOS, a real Python TCP client, a
temporary toggleable `privilege_violation()` lfun appended to the real
bundled `/single/master.c`, reverted via `git checkout` immediately
after, confirmed clean via `git diff --stat`): under `dialect: ldmud`, a
real pending `call_out()` produced the real empty array on denial and
the real one-entry array on grant; `input_to()` returned real `0` on
denial and real `1` on grant with the bang flag set, and the granted
registration incidentally proved itself live end to end beyond the
simple return-code check: the very next line sent over the same
connection was genuinely intercepted by the newly-registered handler
instead of being dispatched as an ordinary command, confirmed by the
connection closing when that handler's own undefined target function
was invoked, the real per-connection error-isolation behavior
(`Server.cpp`) rather than a driver crash. A flags value omitting the
bang bit still returned real `1` even with the master denying
everything, confirming the gate is genuinely conditional on the flag,
live, not just in the unit tests. Under the default FluffOS-dialect
driver, `call_out_info()` still returned the real one-entry array even
with the exact same master actively set to deny, confirmed ungated.
Both scratch processes stopped, `mudlib/single/master.c` reverted,
confirmed clean via `git diff --stat`.

`ROADMAP.md` row 1.7 updated in place with this session's own citations
and live-verification account, appended after the prior update rather
than rewritten. 715 tests passing (up from 709), zero regressions.

Staged with `git add` only, per this project's own standing rule; not
committed.
