// Minimal smoke-test object for the character-literals and
// string-indexing slice ('x' syntax, str[i] returning a byte value).
// create() reproduces the real blocking line in
// secure/daemon/master.c's load_access(), now using an actual string
// and a real character literal instead of the array-of-fields stand-in
// the logical-operators slice used (that stand-in existed only because
// this slice's constructs did not exist yet):
//
//   if(!lines[i] || lines[i] == "" || lines[i][0] == '#') continue;

void create() {
    mixed *lines;
    int i;

    lines = ({ "sword", "", "#comment" });

    i = 0;
    while (i < sizeof(lines)) {
        if (!lines[i] || lines[i] == "" || lines[i][0] == '#') {
            write("skip\n");
        } else {
            write("keep: " + lines[i] + "\n");
        }
        i = i + 1;
    }
}
