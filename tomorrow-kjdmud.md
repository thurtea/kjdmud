# kjdmud: Session Handoff (2026-09-22)

Homepage: https://thurtea.com/kjdmud/
Source: https://github.com/thurtea/kjdmud
Status/roadmap: `docs/dev/ROADMAP.md`, `docs/dev/STATUS.md`, `docs/COMPARISON.md`.

## Resume prompt (paste into Claude Code)

```
Continue kjdmud, a from-scratch C++20 LPC MUD driver. LPC is mudlib-only,
the host runtime stays C++20, see CLAUDE.md's non-negotiable rules
before doing anything (never git commit/push, no em dashes/emojis, real
decisions get written into ROADMAP.md/STATUS.md in the same turn).

Read docs/dev/ROADMAP.md and docs/COMPARISON.md's Open rows first.
Driver work in src/ is the priority over mudlib/library work unless
told otherwise. This session landed: defer(function) (VM.cpp/VM.hpp/
EfunTable.cpp, real FluffOS per-frame LIFO-on-return semantics),
compress()/uncompress() (zlib-backed, EfunTable.cpp), and a mudlib fix
(bold-green "Exits:" line, mudlib/inherit/room.c's new exits_line()/
show_desc(), replacing exit sentences baked into room long() prose).

Before implementing any efun/apply: verify against real source, not
memory. temp/reference/fluffos-2.9-ds2.08/ is the pinned vendored
FluffOS tree this repo's citations mean; temp/ds3.8.2_extracted/.../
fluffos-2.23-ds03/ is a second, narrower one. For anything not vendored
there (this session needed that for defer() and compress(), both newer
than the pinned tree), clone a fresh copy to the scratchpad, cite it,
delete it when done - do not guess from pattern-resemblance.

Pick the next real, well-scoped gap from docs/COMPARISON.md's Open rows
(2.42's compress_file()/uncompress_file() file-on-disk half is a natural
next step after this session's in-memory compress()/uncompress(); other
open rows: 2.34 set_config, 2.39 socket_get_option/socket_set_option,
2.41 domain_stats/author_stats, 2.20 structured error objects). Verify
several "Open" rows are not actually stale before starting new work on
them - this session found four separate stale rows already done
(2.13/2.14, 2.17, 2.22, 2.31, 2.35, 2.38) before defer()/compress() were
picked precisely because they were confirmed genuinely open first.

Implement, add regression tests to test/test_lexer.cpp, full clean
rebuild, ctest 2/2 green, then a live telnet boot check before calling
it done. End with a commit message (never git commit/push it yourself).
```
