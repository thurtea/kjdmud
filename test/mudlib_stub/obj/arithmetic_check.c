// Minimal smoke-test object for the arithmetic-operators slice
// (-, *, /, % as binary expressions, unary negation). create()
// reproduces the real blocking line's arithmetic in
// secure/daemon/master.c:
//
//   write("("+(t/60)+"."+(t%60)+")\n");
//
// with a fixed t value (125, meaning 2 minutes 5 seconds). The
// comparison-and-write shape below, rather than directly
// concatenating the string "(" with the int result of t/60, is
// deliberate: Add only supports string+string, array+array, and
// numeric+numeric (no silent string/number coercion, confirmed and
// kept as specified during the arrays/mappings slice), so string+int
// concatenation is a separate, already-known, not-yet-implemented gap,
// unrelated to this slice's arithmetic opcodes. This mirrors the same
// fix already applied to array_check.c for the same reason.

void create() {
    int t;
    int minutes;
    int seconds;

    t = 125;
    minutes = t / 60;
    seconds = t % 60;

    if (minutes == 2 && seconds == 5) {
        write("elapsed: 2.5\n");
    } else {
        write("elapsed: wrong\n");
    }
}
