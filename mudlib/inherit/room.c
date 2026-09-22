// mudlib:  library
// file:    /inherit/room.c
// purpose: a small mixin giving a static room real, navigable exits.
//          Before this, the only room this mudlib had (the gatehouse,
//          /single/gatehouse.c) was a dead end. There was nowhere to
//          walk to at all. A concrete room opts in by calling
//          set_exits() from its own create() (a mapping of direction
//          name -> destination room path, e.g. (["north":
//          ROOM_WATCH_ROOM])) and, if it also defines its own init() the
//          way gatehouse.c already does (for its wand hand-out), by
//          calling "room::init();" once its own logic is done so both
//          run.
//
// Deliberately plain, not folded into /inherit/base.c: gatehouse.c never
// inherited BASE either (matching wand_of_creation.c's own "no inherits
// at all" style for a driver-apply-only file), so this stays a second,
// separate mixin a room explicitly opts into rather than pulling in
// BASE's own unrelated living/inventory conventions.

private mapping room_exits;
private string room_short;
private string room_long;
private int room_light;
private mapping room_items;

void set_short(string s) {
    room_short = s;
}

void set_long(string s) {
    room_long = s;
}

string short() {
    return room_short;
}

string long() {
    return room_long;
}

string query_short() {
    return room_short;
}

string query_long() {
    return room_long;
}

void set_light(int n) {
    room_light = n;
}

int query_light() {
    return room_light;
}

void add_item(string id, string desc) {
    if (!room_items) {
        room_items = ([]);
    }
    room_items[id] = desc;
}

mapping query_items() {
    return room_items;
}

void set_exits(mapping m) {
    room_exits = m;
}

mapping query_exits() {
    return room_exits;
}

// do_go: one add_action handler shared by every direction this room
// defines. query_verb() recovers which direction was actually typed.
// Registered on the *player* (see init() below and VM::moveObject()'s
// own "Leg 1" comment: a room's init() runs with the mover as
// command_giver, so add_action() here attaches to them, not to this
// room), so it stays live for as long as they are standing here.
int do_go(string arg) {
    string dir;
    object dest;

    dir = query_verb();
    if (!room_exits || !room_exits[dir]) {
        return 0;
    }
    dest = find_object(room_exits[dir]);
    if (!dest) {
        dest = load_object(room_exits[dir]);
    }
    if (!dest) {
        write("That way seems to be blocked.\n");
        return 1;
    }
    this_player()->move(dest);
    return 1;
}

// room::init(). Called explicitly by a concrete room's own init() (a
// bare qualified parent call) once its own logic is done. Registers one
// add_action per real exit this room actually has.
void init() {
    string *dirs;
    int i;

    if (!room_exits) {
        return;
    }
    dirs = keys(room_exits);
    for (i = 0; i < sizeof(dirs); i++) {
        add_action("do_go", dirs[i]);
    }
}

// exits_desc: a plain "north, east" string for a room's own long() to
// embed, so a tester can see where they can go without guessing.
string exits_desc() {
    string *dirs;
    string out;
    int i;

    if (!room_exits || !sizeof(room_exits)) {
        return "none";
    }
    dirs = keys(room_exits);
    out = "";
    for (i = 0; i < sizeof(dirs); i++) {
        out += dirs[i];
        if (i + 1 < sizeof(dirs)) {
            out += ", ";
        }
    }
    return out;
}

// exits_line: a standalone, bold-green "Exits: north, east." line (real
// daemon/terminal.c's own ANSI(p)/ESC(p) macro convention, ported here
// directly: sprintf("%c[" + p + "m", 27) builds a raw ESC (ASCII 27)
// byte ahead of the ANSI code, confirmed against this driver's own
// sprintf %c support. See EfunTable.cpp's own sprintf %c comment for
// the citation). Empty string, not "Exits: none.", when this room has
// no exits at all (matching look.c's own pre-existing "only print an
// Exits line when there is at least one" guard, now centralized here).
string exits_line() {
    if (!room_exits || !sizeof(room_exits)) {
        return "";
    }
    return sprintf("%c[1;32mExits: %s.%c[0m\n", 27, exits_desc(), 27);
}

// show_desc: the one real entry point both an entering player (each
// concrete room's own init(), replacing a bare "write(long());") and
// an explicit "look" (command/look.c) now go through, so both show the
// same long() text plus the same colored exits_line() rather than two
// slightly different hand-rolled versions of the same output.
void show_desc() {
    write(long());
    write(exits_line());
}
