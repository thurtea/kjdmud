# Next Session Handoff: 2026-09-22

Four rows landed this session (`docs/dev/STATUS.md`'s own dated
entries, most recent first): a testing-seam design note
(`docs/dev/TESTING_SEAMS.md`), ZMP mudlib usage docs
(`docs/dev/PROTOCOLS.md`) plus an end-to-end malformed-frame test,
GMCP_ENABLE/MSDP_ENABLE applies (and a real `send_msdp` naming defect
corrected to `send_msdp_variable`), and IAC-escaping for
`sendGmcp()`/`sendMssp()`/`sendMsdp()`. No known gaps left open from
this session's own findings.

This machine's own `temp/` is still absent; a fresh upstream FluffOS
clone for one-off verification worked well multiple times this session
(deleted after each use). Keep doing that before writing driver code
for any new protocol/efun row rather than assuming scope from a prior
summary - re-verifying ZMP rather than trusting the previous session's
own summary is what surfaced the `GMCP_ENABLE`/`MSDP_ENABLE` and
`send_msdp` findings in the first place.

No queued next protocol row right now. Pick the next clear `src/` gap
(`docs/COMPARISON.md` Open rows, or any `src/<module>/instruct.md`), not
a mudlib chase. If it is a new telnet protocol apply/dispatch method,
read `docs/dev/TESTING_SEAMS.md` first for the naming convention.
