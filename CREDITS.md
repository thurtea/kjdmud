# Credits

kjdmud is a from-scratch LPC game driver. Its lexer, parser, code generator,
bytecode VM, object system, and network layer are original implementations.

Dialect support, efun surface, and object lifecycle semantics were shaped by
reading publicly available LPC driver sources and porting documented,
verified behavior so existing mudlib code has a reasonable chance of running
unmodified. Prior art worth naming:

- FluffOS
- LDMud
- DGD (Dworkin's Game Driver)

For a source-cited breakdown of what kjdmud currently reproduces, see
`docs/COMPARISON.md`.
