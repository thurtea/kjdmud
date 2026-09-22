# Next Session Handoff: 2026-09-22

ZMP landed (`docs/dev/STATUS.md` 2026-09-22). `src/proto/instruct.md`'s
own five-protocol table is fully closed, and MSP/ZMP have landed beyond
its original scope too. No queued next protocol row right now; pick the
next clear `src/` gap (`docs/COMPARISON.md` Open rows, or any
`src/<module>/instruct.md`), not a mudlib chase.

Two real, verified findings from this session are flagged but not yet
fixed (`docs/dev/ROADMAP.md`'s own "Verification note"), either one a
reasonable next pick:
- `sendMssp()`/`sendMsdp()`/`sendGmcp()` do not double embedded IAC
  (0xFF) bytes in their payloads the way `sendMspOob()`/`sendZmp()` now
  do (real `telnet_send()`, confirmed against upstream libtelnet).
- Real GMCP/MSDP each have their own `GMCP_ENABLE`/`MSDP_ENABLE` real
  applies (`src/vm/internal/applies`) that this repo's own already-
  shipped `gmcp()`/`msdp()` handling never fires.

This machine's own `temp/` is still absent; a fresh upstream FluffOS
clone for one-off verification worked well twice this session (deleted
after each use) - keep doing that before writing driver code for any
new protocol/efun row rather than assuming scope from a prior summary.
