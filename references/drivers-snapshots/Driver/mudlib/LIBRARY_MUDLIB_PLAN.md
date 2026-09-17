# Library mudlib plan

This file is the scoping document for the bundled mudlib under
`mudlib/`. It was cited from `etc/driver.cfg`, `globals.h`, and the
reeve's rod, then went missing from disk. Restored 2026-09-05.

## What this mudlib is

A small, theme-light LPC library the driver ships and boots. Login,
accounts, four Stonewick rooms, `eval`, and the reeve's rod
(`/clone/wand_of_creation.c`). It is not AetherMUD, not Nightmare, and
not Dead Souls. Patterns may be ported. Files are not copied wholesale
from `temp/`.

## Standing rules

1. Keep Library small. New rooms and inheritables are written here, not
   merged in from a third-party corpus.
2. LPC is the mudlib language. The host driver stays C++20.
3. Palladium material (dice, tables, OCC/race math) stays inside the
   running game. Credit Palladium. Charge nothing. Do not dump a
   downloadable system.
4. Optional-keyword bugs are fixed when this lib or the rod hits them.
   Do not go hunting them in Dead Souls.

## Builder kit (current slice)

Theme-agnostic primitives first:

- `/inherit/object.c`: `id` / `short` / `long` / `move`
- `/inherit/room.c`: exits and `do_go`
- `/command/look.c` and `/command/examine.c`
- Rod commands: `clone`, `purge`, `create`, `edit`, `room`, `exit`
- Wizard gating: `user.c` calls `enable_wizard()`; the rod checks
  `wizardp(this_player())`

World kit next (item / room / NPC inheritables, domain folders,
`save_object` persistence of created graphs) then Rifts content on
that kit, not instead of it.

## What is deliberately out

AetherMUD as the bundled lib. Inventory/combat as a first-slice
requirement. Clickable MXP builder UI. Auto-load persistence of the
rod itself.
