# Credits

amlp is a from-scratch LPC game driver. Its lexer, parser, code generator,
bytecode VM, object system, and network layer are original implementations --
no source code is shared with, copied from, or derived from any other LPC
driver.

That said, amlp did not invent the LPC language or its surrounding driver
conventions out of nothing. Its dialect support, efun surface, and object
lifecycle semantics were built by reading the real, publicly available
source of several existing LPC drivers and porting their documented,
verified behavior, including real quirks and edge cases, so that
existing LPC mudlib code has a reasonable chance of running on amlp
unmodified. Credit for that prior art belongs to the projects and their
maintainers and contributors:

- **FluffOS**: an actively maintained LPC driver descended from the
  original MudOS lineage.
- **LDMud**: an actively maintained LPC driver descended from the
  original LPMud lineage.
- **DGD** (Dworkin's Game Driver): an LPC-family driver with a distinct
  architecture (statedump-based persistence, lightweight objects, atomic
  functions) that amlp draws on for comparison and, in a few specific
  places, real feature support.

None of these projects are affiliated with amlp, endorse it, or are
responsible for it. amlp is not a fork, wrapper, or compatible
reimplementation claiming to be a drop-in replacement for any of them --
it is its own driver that happens to understand several dialects of the
same language family.

For a detailed, source-cited breakdown of exactly which behaviors amlp
does and does not currently reproduce, and how its numbers compare, see
`docs/COMPARISON.md`.
