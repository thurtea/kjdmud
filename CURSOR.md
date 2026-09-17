# CURSOR.md

Standing rules for Cursor agents working in this repository.

1. **Never run `git commit` or `git push`, under any circumstance.**
   Stage changes with `git add` only. When work is done, report what
   changed and provide a complete, ready-to-use commit message for the
   human to run manually. Never include a `Co-Authored-By` line in any
   commit message you draft.

   Amendment (effective immediately): that commit message is one line,
   two only if genuinely needed. State what changed, nothing else: no
   scoping derivation, no citation trail, no test-count history. That
   detail belongs in `docs/dev/STATUS.md`.

2. **No em dashes and no emojis** anywhere in code, comments, commit
   messages, or documentation, unless there is an actual syntax or code
   reason. The em dash character itself is banned. Replace it with real
   grammar: a period and a new sentence, a comma, a semicolon,
   "and"/"but"/"which", or a colon, whichever actually fits. Do not
   substitute a double hyphen for an em dash. That is the same mistake.
   A double hyphen is allowed only as real syntax: a command-line flag
   such as `--output-on-failure`, a decrement operator, or an
   end-of-options token.

3. **Any real decision made in a chat reply must be written into
   `docs/dev/STATUS.md` or `docs/dev/ROADMAP.md` in the same turn it is
   decided.** A license or scope choice, a "drop this" or "rewrite this"
   call, a scoping conclusion reached during investigation: it goes into
   one of those files in the same turn it is decided, not left to be
   relayed secondhand in a later session. This rule was added after two
   documented incidents where a decision relayed only in chat was later
   treated as authoritative by a fresh session, contradicted the actual
   repo state, and wasted real work before being caught.

   Amendment (effective immediately): keep each STATUS.md entry to a few
   lines. Record the decision and what changed, not the derivation.

4. **An inline comment states what the code does and, where genuinely
   non-obvious, a one-line pointer to the real citation** (file:line, or
   an efun/opcode name), **not a full re-derivation of the scoping
   process.** The full derivation belongs in `docs/dev/STATUS.md` and
   the commit message, which already carry it. A comment that explains a
   real behavioral quirk or a deliberate divergence from upstream is
   still valuable and stays; a comment that just narrates "here is why I
   made this change" at essay length does not.

   Amendment (effective immediately): hard limit of 1 to 2 lines of
   comment per logical change, in any source file, with no exception for
   scoping or derivation notes. A citation that genuinely needs
   recording goes in the commit message and `docs/dev/STATUS.md`, never
   in the source.

5. **Git-tracked files carry only what a competent third party needs to
   build and understand the driver:** source, headers, build config, the
   mudlib content required to boot, and the docs that state real current
   status (`README.md`, `CURSOR.md`, `docs/dev/ROADMAP.md`,
   `docs/dev/STATUS.md`). Planning notes, scratch and working docs,
   session summaries, and anything else that is not part of "how do I
   build and run this" live under a gitignored path, not in the tree.

## Authoritative status record

`docs/dev/ROADMAP.md` and `docs/dev/STATUS.md` are the authoritative
status record for this project, not chat history and not session
summaries. Any session must verify a claim against these files, and
against actual `git log` / build / test output, before treating it as
true.
