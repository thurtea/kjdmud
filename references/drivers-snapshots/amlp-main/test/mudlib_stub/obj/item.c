// A single trivial clonable object for the minimal test mudlib -- exists
// only to exercise clone_object() as a live "object creation" check.
// start_room.c clones one of these into itself at load time via move_to().

string query_short() {
    return "a small brass gear";
}

string query_long() {
    return "A small brass gear, scavenged and well-worn.";
}

// move_object() always moves current_object(), so a room's own create()
// cannot place a freshly cloned item into itself directly -- it has to
// call a function on the item itself, which then moves itself.
void move_to(object dest) {
    move_object(dest);
}
