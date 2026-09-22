# Next Session Handoff: 2026-09-22

`json_parse`/`json_serialize` landed (`docs/dev/STATUS.md` 2026-09-22),
real LDMud efuns, dialect-gated to `ldmud` like `db_*`. Live-walked the
bundled mudlib's own `mudlib/domains/rifts/` content over a real telnet
session (character creation, movement, examine, take/drop) - confirmed
working end to end, the user's own stated goal (a Rifts-based mudlib)
is a real, supported target.

`temp/` is restored on this machine from the user's own local
`/home/thurtea/Documents/backups/amlp/temp/` backup - the exact pinned
`reference/fluffos-2.9-ds2.08/` this repo's citations have always
meant, plus `ds3.8.2_extracted/.../fluffos-2.23-ds03/`. Gitignored, not
committed. Prefer citing that directly over a fresh clone for real-
FluffOS verification going forward; still no `temp/ldmud/`, so LDMud
verification still needs a fresh clone (network access confirmed
available, delete after use).

One real, reproduced-but-not-yet-fixed anomaly:
`this_player()->move_object(dest)` silently no-ops when called from
inside the `eval` command's own nested `/tmp_eval_file::eval()`
indirection (see `docs/dev/STATUS.md`'s own dated entry for the full
diagnosis). Lower priority than a general driver defect - the realistic
pattern (an item's own `move()` method, reached through one normal
`call_other` from an add_action verb) already works, proven by `take`/
`drop` succeeding live. Worth a real fix if an admin "goto"-style verb
is ever written through `eval` specifically, not urgent otherwise.

No queued next protocol/efun row. Pick the next clear `src/` gap
(`docs/COMPARISON.md` Open rows - note several are stale, verify before
trusting the raw count; see this session's own several corrections), or
start building out the Rifts mudlib content itself now that the driver
side is confirmed working. If it is a new telnet protocol apply/dispatch
method, read `docs/dev/TESTING_SEAMS.md` first for the naming
convention.
