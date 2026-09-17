// Player object for the minimal test mudlib. Cloned once per connection
// by /obj/login.c, which sets the name and exec()s the connection onto
// it before calling setup().

string name;

void create() {
    name = "someone";
}

void set_name(string str) {
    name = str;
}

string query_name() {
    return name;
}

// Needed so cmd_look()'s inventory listing shows other players by name
// instead of silently getting monostate back from an undefined function
// (call_other on a function a target object does not define returns 0,
// not an error -- confirmed live: without this, another player in the
// room showed up as "You see 0 here.").
string query_short() {
    return name;
}

// Called by login.c immediately after exec() rebinds the connection to
// this object -- add_action()/enable_commands() need the connection
// already bound here to resolve command_giver correctly (see
// EfunTable.cpp's resolveCommandGiver()).
void setup() {
    enable_commands();
    add_action("cmd_look", "look");
    add_action("cmd_north", "north");
    add_action("cmd_south", "south");
    add_action("cmd_say", "say");
    add_action("cmd_boot", "boot");
    move_object("/rooms/start_room");
    write("Welcome, " + name + ".\n");
    cmd_look("");
}

void cmd_look(string arg) {
    object room;
    object *inv;
    int i;

    room = environment(this_object());
    if (!room) {
        write("You are nowhere.\n");
        return;
    }

    write(room->query_short() + "\n");
    write(room->query_long() + "\n");

    inv = all_inventory(room);
    for (i = 0; i < sizeof(inv); i++) {
        if (inv[i] != this_object()) {
            write("You see " + inv[i]->query_short() + " here.\n");
        }
    }
}

void cmd_north(string arg) {
    go("north");
}

void cmd_south(string arg) {
    go("south");
}

void go(string dir) {
    object room;
    mapping exits;
    string dest;

    room = environment(this_object());
    if (!room) {
        write("You are nowhere.\n");
        return;
    }

    exits = room->query_exits();
    dest = exits[dir];
    if (!dest) {
        write("You can't go that way.\n");
        return;
    }

    move_object(dest);
    write("You go " + dir + ".\n");
    cmd_look("");
}

// Real FluffOS's own link-death apply (APPLY_NET_DEAD, comm.c's
// remove_interactive()): fired by the driver when this player's
// connection drops (EOF/read error), before the connection is actually
// torn down. Kept deliberately minimal for this stub mudlib -- just
// enough to prove live that the driver's new Server::fireNetDeadIfLinkDead()
// genuinely reaches it, not a real save/reconnect implementation.
void net_dead() {
    object room;
    object *inv;
    int i;

    room = environment(this_object());
    if (!room) return;

    inv = all_inventory(room);
    for (i = 0; i < sizeof(inv); i++) {
        if (inv[i] != this_object()) {
            message("say", name + " has gone link-dead.\n", inv[i]);
        }
    }
}

// Minimal stand-in for a real mudlib's admin "boot"/kick command --
// destructs another player's object by name from a room. Exists in this
// stub purely to exercise destruct()'s own connection-closing fix live
// (EfunTable.cpp): the target's connection must actually close, not just
// the caller's.
void cmd_boot(string arg) {
    object room;
    object *inv;
    int i;

    if (!arg || arg == "") {
        write("Boot whom?\n");
        return;
    }

    room = environment(this_object());
    if (!room) return;

    inv = all_inventory(room);
    for (i = 0; i < sizeof(inv); i++) {
        if (inv[i] != this_object() && inv[i]->query_name() == arg) {
            write("You boot " + arg + ".\n");
            destruct(inv[i]);
            return;
        }
    }
    write("No one named " + arg + " here.\n");
}

void cmd_say(string arg) {
    object room;
    object *inv;
    int i;

    if (!arg || arg == "") {
        write("Say what?\n");
        return;
    }

    write("You say: " + arg + "\n");

    room = environment(this_object());
    if (!room) return;

    inv = all_inventory(room);
    for (i = 0; i < sizeof(inv); i++) {
        if (inv[i] != this_object()) {
            message("say", name + " says: " + arg + "\n", inv[i]);
        }
    }
}
