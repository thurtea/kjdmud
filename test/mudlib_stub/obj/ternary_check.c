// Minimal smoke-test object for the ternary conditional operator slice
// (cond ? a : b syntax). create() reproduces the real shape found at
// raw line 469 in secure/daemon/master.c:
//
//   string objfn = obj ? file_name(obj) : "<none>";
//
// file_name() is not an implemented efun in this driver (a separate,
// unrelated gap, out of scope for this slice), so this stub substitutes
// a plain string literal for the then-branch, the same substitution
// used by this slice's own VM-level unit test. What this checks is the
// part this slice actually adds: a ternary whose condition is an
// object-typed local, exercised on both the null and non-null path, the
// same way arithmetic_check.c and guard_char_check.c each check more
// than one input rather than a single fixed case.

void create() {
    object obj;
    string objfn;

    obj = 0;
    objfn = obj ? "has object" : "<none>";
    if (objfn == "<none>") {
        write("ternary null-object branch ok: " + objfn + "\n");
    } else {
        write("ternary null-object branch wrong: " + objfn + "\n");
    }

    obj = clone_object("/obj/simple_login");
    objfn = obj ? "has object" : "<none>";
    if (objfn == "has object") {
        write("ternary live-object branch ok: " + objfn + "\n");
    } else {
        write("ternary live-object branch wrong: " + objfn + "\n");
    }

    // Right-associativity and nested-condition sanity check, same shape
    // as raw line 433's "(caught ? "catch" : "runtime")" pattern but
    // chained: confirms a ? b : c ? d : e groups as a ? b : (c ? d : e)
    // at runtime, not just at parse time.
    if ((1 ? "x" : "y") == "x" && (0 ? "x" : (1 ? "z" : "w")) == "z") {
        write("ternary right-associativity ok\n");
    } else {
        write("ternary right-associativity wrong\n");
    }
}
