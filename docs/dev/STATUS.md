# STATUS

**2026-09-19: query_ip_number/name honor object arg.** Both efuns resolve
an optional interactive object through `InteractiveRegistry` (same
pattern as `query_ip_port`) instead of only `OutputContext::current()`.
No-arg still prefers the current connection, then command_giver.
`query_ip_name` stays numeric-only (no blocking DNS). Website footers
dropped affiliation disclaimers; CREDITS trimmed to a short prior-art
list. Suite green (2/2 tests).

**2026-09-19: query_ip_port uses accept port.** Multi-port `listen:` meant
`query_ip_port` returning only `Config::port()` was wrong for websocket
and TLS listeners. `Connection` now stores the accept port
(`Server::onNewConnection`); the efun reads it via
`InteractiveRegistry`. Regression updated. Suite green (2/2 tests).

**2026-09-19: Public site and snapshot purge.** Added `website/` (overview,
download, documentation, WebSocket client) modeled as a simple project
site. GitHub Pages workflow deploys `website/` from `main`. README and
INSTALL now describe only the current driver; INSTALL points the browser
client at `website/client.html`. Removed `references/` (old local driver
snapshot trees) from the repository. Deleted `research.md` and
`docs/dev/TODAY.md`. FluffOS citation sources remain under gitignored
`temp/` for local development. Next: small FluffOS driver gap in `src/`,
not another mudlib boot chase.

**2026-09-17: MUD socket read framing.** `Server::pollSockets` assembles
the 4-byte length header and save_variable body, then
`parseRestoreVariableTopLevel` for the read callback. Save/restore
helpers moved to `vm/SaveVariable` so net can use them. Round-trip
regression green.

**2026-09-17: Driver-first scope.** Product work stays in the driver
(`src/`). External mudlibs are probe-only; do not chase RiftsMUD /
Nightmare / library completion as the next goal. Prefer the next clear
driver gap over another mudlib boot matrix.

**2026-09-17: TMI-2 live look/move.** `clone_object`/`new` now pass
trailing args to `create()` (SOCKET styles were ignored before). MUD
socket mode (0) creates TCP and frames `socket_write` with
`htonl(len)+save_variable`. Non-blocking connect treats
EPERM/ENETUNREACH/EHOSTUNREACH like EINPROGRESS so I3 create can finish
when outbound routes are denied. Live on 4210: create, ENTER, quad
room `look`, `say`, `inventory`, `north` to MudOS room. Suite green
for new clone/MUD regressions.

**2026-09-17: TMI-2 live login.** Port 4210 adapter. New-character
flow completes (name letters-only, confirm, password, gender, race,
email, real name). Wizard grant, `say`, and `inventory` work. `look`
fails (no room): `I3` create dies on socket send `Descriptor out of
range`, `channels` create fails, `user::setup` aborts before
`complete_setup`. Groups "User ... not found" is mudlib noise. Next:
fix I3/channels so setup places the player. Handoff:
`notes/tomorrow.md`.

**2026-09-17: TMI-2 boot control.** Fetched `tmi2_fluffos_v3` under
gitignored `temp/`. First blockers fixed so simul_efun loads and the
driver reaches Ready: (1) `maskHashQuote` no longer mangles `'#'`
character constants; (2) `#ifdef 0` / `#ifndef 0` rewritten for system
cpp; (3) bare Ident `array` is a type only when a declaration follows,
so `array = array[offset..<1]` assigns; (4) `..` range ops masked with
spaces before cpp so macros like `D_IN` still expand (string/comment
aware). Suite green (949 checks, 0 fail). Bounded boot: Simul_efun
loaded, Master loaded, Ready on port 4201.

**2026-09-17: sscanf `%i`/`%X`/`%o`.** Integer alias, uppercase hex, and
octal format specs match FluffOS `inter_sscanf` bases. Adjacent `%s`
lookahead treats the new specs the same way. Three regression tests.
Suite green (944 checks, 0 fail).

**2026-09-17: kjdmud foundation.** Promoted `aemlpc-main` to the repo
root as kjdmud. Identity rename (`aemlpc` to `kjdmud`) for CMake,
`include/kjdmud/`, binary, and docs. Persist magic `AMLPSTATE1` kept.
Older trees archived under `references/drivers-snapshots/` (no nested
`.git` or `build/`). Apple ld: `object_manager_tests` no longer passes
GNU `--start-group` on macOS. Clean build + CTest green: 2/2 tests,
941 checks in `kjdmud_tests`, 0 fail. Bundled mudlib boots to Ready
for connections (`etc/driver.cfg` 5 iterations).

**2026-09-13: sprintf `%'X'`.** Quoted pad strings now fill field width
(FluffOS `add_pad`). Cited
`temp/external/lpuni_fluffos_v1/fluffos-2.9-ds2.07/sprintf.c` line 926;
canonical `temp/reference/fluffos-2.9-ds2.08` is absent. Suite green (941
checks, 0 fail).

**2026-09-13: sprintf `%@`.** Array-spread now applies the rest of the
specifier to each element. Cited
`temp/external/lpuni_fluffos_v1/fluffos-2.9-ds2.07/sprintf.c` `INFO_ARRAY`;
canonical `temp/reference/fluffos-2.9-ds2.08` is absent. Suite green (940
checks, 0 fail).

**2026-09-13: sprintf `%s` zero.** Integer 0 and a missing mapping key now
print `0` (FluffOS `NULL_MSG`). Cited
`temp/external/lpuni_fluffos_v1/fluffos-2.9-ds2.07/sprintf.c` and `config.h`;
canonical `temp/reference/fluffos-2.9-ds2.08` is absent. Suite green (939
checks, 0 fail).

**2026-09-13: sprintf `%X`.** Uppercase hex now matches C `%X`. Cited
`temp/external/lpuni_fluffos_v1/fluffos-2.9-ds2.07/sprintf.c` `INFO_T_C_HEX`;
canonical `temp/reference/fluffos-2.9-ds2.08` is absent. Suite green (938 checks, 0 fail).

**2026-09-13: sprintf `%0*`.** Zero-padded dynamic field width now pads
like `%0Nd`. Cited `temp/external/lpuni_fluffos_v1/fluffos-2.9-ds2.07`
`sprintf.c` (field-size `0` then `*`); canonical
`temp/reference/fluffos-2.9-ds2.08` is absent. Suite green (937 checks, 0 fail).

**2026-09-13: G6 three-room loop.** `watch_post` is linked north of
`lower_gate` with a floodlight scenery item. Market tin, hawker, and
slag bin unchanged. Suite green (937 checks, 0 fail).

**2026-09-13: G6 first rooms.** Two linked Chi-Town 'Burbs rooms under
`mudlib/domains/rifts/` (lower gate and market lane), one scenery poster,
one takeable ration tin, one hawker NPC. Non-portables persist via the
G5 graph. G1-G5 kit unchanged. Suite green (936 checks, 0 fail).

**2026-09-13: `sscanf` regexp format.** `%(regexp)` now matches and
assigns an anchored PCRE2 match, including `%s%(regexp)` lookahead.
Cited `temp/external/lpuni_fluffos_v1/fluffos-2.9-ds2.07/interpret.c`
`inter_sscanf`; canonical `temp/reference/fluffos-2.9-ds2.08` is absent.
Suite green (935 checks, 0 fail).

**2026-09-12: array `|` union.** BitOr on two arrays is now FluffOS
`union_array` (all of left, then each right element not already in
left). Cited `temp/external/lpuni_fluffos_v1/fluffos-2.9-ds2.07`
`eoperators.c` `f_or` and `array.c` `union_array`;
`temp/reference/fluffos-2.9-ds2.08` is not on disk. `^` stays ints
only. Suite green (933, 0 fail).

**2026-09-12: dump_state width>1.** Persist now writes extra mapping
columns as `M<count>w<width>:`. Width-1 dumps stay `M<count>:`. Magic
`AMLPSTATE1` unchanged. Suite green (932, 0 fail).

**2026-09-12: save_object write-side.** Width>1 mappings no longer throw;
extra columns write as `key:v0;v1` and restore. Object, closure, and
buffer write nothing (FluffOS `save_svalue` has no case; cited
`temp/external/lpuni_fluffos_v1/fluffos-2.9-ds2.07/object.c` because
`temp/reference/fluffos-2.9-ds2.08` is not on disk). Restore of those
slots, and of older files that wrote `0`, is integer 0. `dump_state`
still throws on width>1. Suite green (931, 0 fail).

**2026-09-12: identity rename.** Residual `amlp` driver-name strings are
now `kjdmud` (boot text, default `mud_name`, `KJDMUD_MAX_ITERATIONS`,
tmp/test prefixes, instruct include paths). Targets were already
`kjdmud` / `kjdmud_tests`. Persist dump magic `AMLPSTATE1` left as the
on-disk format token. Suite green (929, 0 fail).

**2026-09-12: docs reset.** `ROADMAP.md` and `STATUS.md` rewritten from
scratch as short driver-first records. The driver is the product.
`mudlib/` is the minimum ship Library only. No world-content or Rifts
work on the roadmap. LPC stays mudlib-only; the host stays C++20.

Do not treat chat as status. Verify against these two files, `git log`,
build, and test.

Future STATUS entries stay a few lines each: the decision and what
changed, not the derivation.
