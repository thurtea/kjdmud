# Wand of creation scoping

Restored 2026-09-05. Cited from `/clone/wand_of_creation.c`. The on-disk
file keeps that name because `test/test_lexer.cpp` reads it by path.
Players see "the reeve's rod".

## What the rod is

An in-game builder, not a wizard shell. Held-only. Wizard-gated. It
requisitions copies, clears rubble, writes new LPC under
`/data/created/`, edits those files, and links rooms without leaving
the game.

## Commands

| Verb | Role |
|------|------|
| `clone <path>` | `clone_object` then `ob->move` (inventory, else room) |
| `purge <id>` | `destruct` a non-living object in the room |
| `create <name>` | write `/data/created/<name>.c` inheriting `/inherit/object`, clone, place |
| `edit <id> <text>` | rewrite a created file's long desc and `reload_object` |
| `room <name>` | write a created room inheriting `/inherit/room` |
| `exit <dir> <path>` | `set_exits` on the current room and re-run `init` |

`look` / `examine` are ordinary commands, not rod verbs.

## Real efuns this file relies on

`clone_object`, `destruct`, `present`, `environment`, `this_player`,
`write`, `write_file`, `file_size`, `rm`, `find_object`, `load_object`,
`reload_object`, `file_name`, `wizardp`, `living`, `catch`,
`replace_string`, `move_object` (only from inside `move()`).

`->move()` is a mudlib function. call_other does not fall back to the
`move_object` efun. Every created file inherits `/inherit/object` so
that call is real.

## Deliberately left out

Review / `_qcs` / auto-load of the rod. A full in-game source editor.
Permission models beyond `wizardp` + held. Merging AetherMUD tools.
`look` used to be absent (stock Lil never shipped one); that gap is
closed by `/command/look.c`.
