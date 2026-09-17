// Minimal smoke-test object for the logical-operators slice (||, &&, !).
// create() reproduces the shape of the blocking line in
// secure/daemon/master.c's load_access():
//
//   if(!lines[i] || lines[i] == "" || lines[i][0] == '#') continue;
//
// using an array-of-fields in place of raw string character indexing,
// since character literals and string-character indexing are a
// separate, still-unimplemented gap, out of scope for this slice. The
// guard below keeps the same structure: a null/empty check, then an
// indexed read that would be unsafe (out of bounds) if the earlier
// condition had not already short-circuited the rest of the ||
// chain. If short-circuiting were broken, the empty-array record
// would crash the driver instead of printing "skip".

void create() {
    mixed *records;
    mixed *fields;
    int i;

    records = ({ ({ "sword", "read" }), ({}), ({ "#", "comment" }) });

    i = 0;
    while (i < sizeof(records)) {
        fields = records[i];

        if (sizeof(fields) == 0 || fields[0] == "#") {
            write("skip\n");
        } else {
            write("keep: " + fields[0] + "\n");
        }

        i = i + 1;
    }
}
