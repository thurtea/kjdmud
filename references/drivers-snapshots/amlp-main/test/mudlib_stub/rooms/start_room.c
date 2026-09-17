// Starting room for the minimal test mudlib. Loaded once (cached, not
// cloned) the first time anyone moves into it -- create() clones one
// item into itself at that point, which is the "object creation" check.

mapping exits;

void create() {
    exits = (["north": "/rooms/second_room"]);
    clone_object("/obj/item")->move_to(this_object());
}

string query_short() {
    return "Starting Room";
}

string query_long() {
    return "A small bare room. This is the starting point for the minimal test mudlib.";
}

mapping query_exits() {
    return exits;
}
