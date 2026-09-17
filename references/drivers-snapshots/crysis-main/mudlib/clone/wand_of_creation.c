// mudlib:  library
// file:    /clone/wand_of_creation.c
// purpose: the wand of creation -- a held, wizard-gated in-game builder.
//          clone/purge/create/edit/room/exit. Real efuns only.

// "->" call_other never falls back to the move_object() efun, so a
// placed object must define its own move(); this file and the create
// skeleton both do.
int move(mixed dest) {
    return move_object(dest);
}

string *wand_ids;

void create() {
    wand_ids = ({ "wand", "wand of creation", "creation wand" });
}

int id(string arg) {
    return arg && member_array(arg, wand_ids) != -1;
}

string short() {
    return "a wand of creation";
}

string long() {
    return
        "A slender wand humming with creative potential. It lets its\n"
        "wielder clone existing objects, purge unwanted ones, edit and\n"
        "build new ones, and link rooms together. Usable only while held.\n"
        "Commands: clone <path>, purge <id>, create <name>,\n"
        "          edit <id> <text>, room <name>, exit <dir> <path>\n";
}

static int held() {
    return environment(this_object()) == this_player();
}

static int may_use() {
    if (!held()) {
        write("You are not holding the wand of creation.\n");
        return 0;
    }
    if (!this_player() || !wizardp(this_player())) {
        write("The wand does not answer you.\n");
        return 0;
    }
    return 1;
}

// Registered unconditionally: colocation with a player is the one moment
// this driver calls init() on a plain item. held() is re-checked per
// command instead.
void init() {
    add_action("cmd_clone", "clone");
    add_action("cmd_purge", "purge");
    add_action("cmd_create", "create");
    add_action("cmd_edit", "edit");
    add_action("cmd_room", "room");
    add_action("cmd_exit", "exit");
}

// clone <path>: living things go to the room, everything else prefers
// inventory then falls back to the room.
int cmd_clone(string str) {
    object ob, env;
    string err;

    if (!may_use()) {
        return 1;
    }
    if (!str || !sizeof(str)) {
        write("Clone what?\n");
        return 1;
    }
    err = catch(ob = clone_object(str));
    if (err || !ob) {
        write("Cannot clone: " + str + (err ? " " + err : "") + "\n");
        return 1;
    }

    env = environment(this_player());
    if (living(ob)) {
        if (env && !catch(ob->move(env))) {
            write("Cloned into the room: " + str + "\n");
        } else {
            write("Clone succeeded but could not be placed; destructing it.\n");
            destruct(ob);
        }
        return 1;
    }
    if (!catch(ob->move(this_player()))) {
        write("Cloned into your inventory: " + str + "\n");
    } else if (env && !catch(ob->move(env))) {
        write("Cloned into the room: " + str + "\n");
    } else {
        write("Clone succeeded but could not be placed; destructing it.\n");
        destruct(ob);
    }
    return 1;
}

// purge <id>: destruct a non-living object in the room.
int cmd_purge(string str) {
    object ob;

    if (!may_use()) {
        return 1;
    }
    if (!str || !sizeof(str)) {
        write("Purge what?\n");
        return 1;
    }
    ob = present(str, environment(this_player()));
    if (!ob) {
        write("Not here: " + str + "\n");
        return 1;
    }
    if (living(ob)) {
        write("Cannot purge living objects.\n");
        return 1;
    }
    write("Purged: " + str + "\n");
    destruct(ob);
    return 1;
}

// create <name>: write raw LPC to /data/created/<name>.c, then
// load/clone/place it. Same write-then-load idiom as command/eval.c.
int cmd_create(string str) {
    string fname, path, body;
    object ob, room;

    if (!may_use()) {
        return 1;
    }
    if (!str || !sizeof(str)) {
        write("Create what? (usage: create <name>)\n");
        return 1;
    }
    fname = replace_string(str, " ", "_");
    path = "/data/created/" + fname;

    if (file_size(path + ".c") != -1) rm(path + ".c");
    if (find_object(path)) destruct(find_object(path));

    // Generated files inherit /inherit/item (weight + value over
    // /inherit/object); both default to 1, no verb tunes them yet.
    body = "// made by the wand of creation\n"
        "inherit \"/inherit/item\";\n"
        "void create() {\n"
        "    set_short(\"" + str + "\");\n"
        "    set_long(\"" + str + ", freshly made by the wand of creation.\\n\");\n"
        "    set_ids(({ \"" + str + "\" }));\n"
        "    set_weight(1);\n"
        "    set_value(1);\n"
        "}\n";
    write_file(path + ".c", body, 1);

    ob = load_object(path);
    if (!ob) {
        write("Creation failed: could not compile " + path + ".c\n");
        return 1;
    }
    ob = clone_object(path);
    if (!ob) {
        write("Creation failed: could not clone " + path + ".c\n");
        return 1;
    }

    room = environment(this_player());
    if (room && !catch(ob->move(room))) {
        write("Created and placed here: " + str + "\n");
    } else if (!catch(ob->move(this_player()))) {
        write("Created into your inventory: " + str + "\n");
    } else {
        write("Created but could not be placed; destructing it.\n");
        destruct(ob);
    }
    return 1;
}

int cmd_edit(string str) {
    string id, rest, leaf, path, body, old_short;
    object ob;

    if (!may_use()) {
        return 1;
    }
    if (!str || sscanf(str, "%s %s", id, rest) != 2) {
        write("Usage: edit <id> <new description>\n");
        return 1;
    }
    ob = present(id, this_player());
    if (!ob) {
        ob = present(id, environment(this_player()));
    }
    if (!ob) {
        write("Not here: " + id + "\n");
        return 1;
    }
    if (sscanf(file_name(ob), "/data/created/%s", leaf) != 1) {
        write("Only created objects can be edited.\n");
        return 1;
    }
    path = "/data/created/" + leaf;
    old_short = ob->short();
    if (!old_short || old_short == "") {
        old_short = id;
    }
    // Keep the item skeleton so edit does not downgrade it to a bare
    // object; weight/value reset to the create default.
    body = "// made by the wand of creation\n"
        "inherit \"/inherit/item\";\n"
        "void create() {\n"
        "    set_short(\"" + old_short + "\");\n"
        "    set_long(\"" + rest + "\\n\");\n"
        "    set_ids(({ \"" + id + "\" }));\n"
        "    set_weight(1);\n"
        "    set_value(1);\n"
        "}\n";
    write_file(path + ".c", body, 1);
    reload_object(ob);
    write("Rewrote " + path + ".c\n");
    return 1;
}

int cmd_room(string str) {
    string fname, path, body;
    object ob;

    if (!may_use()) {
        return 1;
    }
    if (!str || !sizeof(str)) {
        write("Usage: room <name>\n");
        return 1;
    }
    fname = replace_string(str, " ", "_");
    path = "/data/created/" + fname;
    body = "// room made by the wand of creation\n"
        "inherit \"/inherit/room\";\n"
        "void create() {\n"
        "    set_exits(([]));\n"
        "}\n"
        "string short() { return \"" + str + "\"; }\n"
        "string long() { return \"" + str + ".\\n\"; }\n";
    write_file(path + ".c", body, 1);
    if (find_object(path)) {
        destruct(find_object(path));
    }
    ob = load_object(path);
    if (!ob) {
        write("Room compile failed: " + path + ".c\n");
        return 1;
    }
    write("Room ready at " + path + ". Use: exit <dir> " + path + "\n");
    return 1;
}

int cmd_exit(string str) {
    string dir, dest;
    object room;
    mapping m;

    if (!may_use()) {
        return 1;
    }
    if (!str || sscanf(str, "%s %s", dir, dest) != 2) {
        write("Usage: exit <dir> <path>\n");
        return 1;
    }
    room = environment(this_player());
    if (!room) {
        write("You are nowhere.\n");
        return 1;
    }
    m = room->query_exits();
    if (!m) {
        m = ([]);
    }
    m[dir] = dest;
    room->set_exits(m);
    room->init();
    write("Exit " + dir + " now leads to " + dest + ".\n");
    return 1;
}
