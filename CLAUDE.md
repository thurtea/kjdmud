# CLAUDE.md

Instructions for Claude Code when working in this repository.

## Non-negotiable rules

These rules have governed this project's entire development history and
must continue unchanged.

1. **Never run `git commit` or `git push`, under any circumstance, for any
   reason.** Stage with `git add` only. When work is done, report what
   changed and provide a complete, ready-to-use commit message for the human
   to run manually. Never include a `Co-Authored-By` line in any commit
   message you draft.

2. **No em dashes and no emojis anywhere** in code, comments, commit
   messages, or documentation, unless there is an actual syntax or code
   reason. Use a period, comma, or start a new sentence instead.

3. **Any real decision made in a chat reply must be written into
   STATUS.md or ROADMAP.md in the same turn it is decided.** A license
   choice, a "drop this" or "rewrite this" call, a scoping conclusion:
   write it into the file immediately, not left to be relayed secondhand
   in a later session.

4. **An inline source comment states what the code does and, where
   genuinely non-obvious, a one-line pointer to the real citation**
   (file:line, or an efun/opcode name) **-- not a re-derivation of the
   whole scoping process.** The full derivation belongs in STATUS.md and
   the commit message, which already carry it; writing it a second time,
   at essay length, above the code itself is what pushed comment density
   past 40% in files like `src/object/ObjectManager.cpp` (measured
   2026-09-04: 44%, 763 of 1,736 lines). A comment that explains a real
   behavioral quirk or a deliberate divergence from upstream is still
   valuable and stays; a comment that narrates "here is why I made this
   change" at multi-paragraph length does not.

## Orientation

- `docs/dev/ROADMAP.md` is the master phase/row tracker. Its checkboxes
  are the authoritative signal for what is actually done.
- `docs/dev/STATUS.md` is the complete dated development log, most recent
  entries first. There is no separate archive file.
- Every `src/<module>/` has its own `instruct.md` describing that module's
  task backlog. These frame tasks as open regardless of actual completion
  status, so they are **not** a live status signal. Only
  `docs/dev/ROADMAP.md`'s checkboxes and `docs/dev/STATUS.md`'s dated
  entries are.
- `temp/reference/fluffos-2.9-ds2.08/` is the vendored real FluffOS 2.9
  source used for every citation throughout this repo (`efun_defs.c`,
  `func_spec.c`, `object.c`, `simulate.c`, etc.). It is intentionally
  untracked (relocated 2026-08-17 from its previous tracked location at
  `reference/fluffos-2.9-ds2.08/`, alongside the six vendored mudlib
  corpora already under `temp/`, none of which are tracked either -- see
  `.gitignore`'s own `temp/` line and commit 777c9d7, which formalized
  the relocation to `temp/`).
  Because it is gitignored, it will not appear in a fresh clone or `git
  status` and must be manually present on disk for any verification work
  that cites it -- if it is missing, say so rather than guessing at real
  behavior from memory or by pattern-resemblance to a similar-sounding
  construct. Provenance: this is the same FluffOS 2.9 (ds2.08 patchlevel)
  tree carried through this project's entire history, previously relocated
  once before (from `mudlib/nightmare3_fluffos_v2/fluffos-2.9-ds2.08/` to
  `driver/reference/fluffos-2.9-ds2.08/` during the LDMud-style restructure,
  then to its now-former tracked location `reference/fluffos-2.9-ds2.08/`)
  -- never re-downloaded or re-derived, so its own internal `README.md`/
  `LLM_BREADCRUMB.md` still describe themselves in terms of even earlier
  locations and are stale prose, not a provenance concern.
- `temp/ds3.8.2_extracted/ds3.8.2/fluffos-2.23-ds03/` is a second real,
  on-disk, gitignored FluffOS source tree, narrower in scope than the
  canonical `temp/reference/fluffos-2.9-ds2.08/` above: it is the exact
  driver version Dead Souls 3.8.2 itself ships with, bundled inside
  that mudlib's own extracted archive rather than placed under `temp/
  reference/`. Citations against it are tied specifically to Dead Souls
  3.8.2's own bundled driver version, not general cross-repo citations
  -- a missing-file check against only the canonical path above must
  not conclude this tree does not exist on disk; check this path too.
