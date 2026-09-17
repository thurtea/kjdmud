// Minimal smoke-test object for the arrays/mappings/indexing/'+' slice.
// create() exercises, in order: an array-typed local variable
// declaration, a non-empty array literal, an empty mapping literal,
// indexed assignment into a mapping, sizeof() on the array, indexed
// read from the array, indexed read from the mapping, and string
// concatenation via '+' (string with string, not string with int,
// since Add deliberately has no silent string/number coercion).
// Cloned from master.c's create() the same way simple_login.c already
// is.

void create() {
    mixed *items;
    mapping scores;
    int total;

    items = ({ "sword", "shield", "potion" });
    scores = ([]);
    scores["sword"] = 10;

    total = sizeof(items);

    if (total == 3) {
        write("items: 3\n");
    } else {
        write("items: wrong\n");
    }

    write("first: " + items[0] + "\n");

    if (scores["sword"] == 10) {
        write("sword score: 10\n");
    } else {
        write("sword score: wrong\n");
    }
}
