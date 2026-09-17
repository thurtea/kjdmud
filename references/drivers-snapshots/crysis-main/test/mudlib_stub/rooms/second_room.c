// Second room for the minimal test mudlib -- exists only to give
// movement something real to prove (move_object() actually relocating
// an object between two distinct rooms, not just staying put).

mapping exits;

void create() {
    exits = (["south": "/rooms/start_room"]);
}

string query_short() {
    return "Second Room";
}

string query_long() {
    return "A second small bare room, connected back south to the starting room.";
}

mapping query_exits() {
    return exits;
}
