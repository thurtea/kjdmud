// mudlib:  library
// file:    /clone/wand_of_creation.c
// purpose: the wand of creation. A held, wizard-gated in-game builder.
//          clone/purge/create/edit/room/exit/npc/domain. Real efuns only.

#include <worldkit.h>

// "->" call_other never falls back to the move_object() efun, so a
// placed object must define its own move(); this file and the create
// skeleton both do.
int move(mixed dest) {
    return move_object(dest);
}

string *wand_ids;
string active_domain;

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
        "          edit <id> <text>, room <name>, exit <dir> <path>,\n"
        "          npc <name>, domain [name], save\n";
}

string query_domain() {
    return active_domain;
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

static int valid_domain_name(string name) {
    if (!name || !sizeof(name)) {
        return 0;
    }
    if (strsrch(name, "/") != -1) {
        return 0;
    }
    return 1;
}

static void ensure_domain_dirs(string name) {
    mkdir(DOMAINS_DIR);
    mkdir(DOMAINS_DIR + "/" + name);
    mkdir(DOMAINS_DIR + "/" + name + "/" + DOMAIN_ITEMS);
    mkdir(DOMAINS_DIR + "/" + name + "/" + DOMAIN_ROOMS);
    mkdir(DOMAINS_DIR + "/" + name + "/" + DOMAIN_NPCS);
}

// Domain set: /domains/<name>/<kind>/. Otherwise the legacy created dir.
static string write_dir(string kind) {
    if (active_domain && sizeof(active_domain)) {
        ensure_domain_dirs(active_domain);
        return DOMAINS_DIR + "/" + active_domain + "/" + kind;
    }
    return CREATED_DIR;
}

static string write_path(string fname, string kind) {
    return write_dir(kind) + "/" + fname;
}

static void maybe_save() {
    if (active_domain && sizeof(active_domain)) {
        catch(DOMAIN_D->save_domain(active_domain));
    }
}

// Real MUD convention (see this file's own wand_ids above): a player
// should be able to refer to a multi-word created item by any of its
// significant words, not only by the exact full name they typed at
// creation. set_ids(({ str })) alone (this file's own original
// generation) only ever matched that one literal string, so a "rusty
// key" made via "create a rusty key" could never be found again by
// "key" or "rusty key" once the room contained anything else. Splits
// on spaces, drops articles/"of", dedupes, and always keeps the full
// name too.
static string *id_stopwords = ({ "a", "an", "the", "of" });

static string *ids_for_name(string name) {
    string *words, *ids;
    int i;

    ids = ({ name });
    words = explode(name, " ");
    for (i = 0; i < sizeof(words); i++) {
        if (!words[i] || !sizeof(words[i])) {
            continue;
        }
        if (member_array(words[i], id_stopwords) != -1) {
            continue;
        }
        if (member_array(words[i], ids) == -1) {
            ids += ({ words[i] });
        }
    }
    return ids;
}

// Renders an id array as LPC array-literal source text, for embedding
// directly into a generated file's own set_ids(...) call.
static string ids_literal(string *ids) {
    string out;
    int i;

    out = "({ ";
    for (i = 0; i < sizeof(ids); i++) {
        out += "\"" + ids[i] + "\"";
        if (i + 1 < sizeof(ids)) {
            out += ", ";
        }
    }
    out += " })";
    return out;
}

static string created_source_path(object ob) {
    string leaf, dom, kind;

    if (sscanf(file_name(ob), CREATED_DIR "/%s", leaf) == 1) {
        return CREATED_DIR "/" + leaf;
    }
    if (sscanf(file_name(ob), DOMAINS_DIR "/%s/%s/%s", dom, kind, leaf) == 3) {
        return DOMAINS_DIR "/" + dom + "/" + kind + "/" + leaf;
    }
    return 0;
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
    add_action("cmd_npc", "npc");
    add_action("cmd_domain", "domain");
    add_action("cmd_save", "save");
}

int cmd_domain(string str) {
    if (!may_use()) {
        return 1;
    }
    if (!str || !sizeof(str)) {
        if (active_domain && sizeof(active_domain)) {
            write("Active domain: " + active_domain + ".\n");
        } else {
            write("No domain set. Writes go to " + CREATED_DIR + ".\n");
        }
        return 1;
    }
    if (str == "none") {
        active_domain = 0;
        write("Domain cleared. Writes go to " + CREATED_DIR + ".\n");
        return 1;
    }
    str = replace_string(str, " ", "_");
    if (!valid_domain_name(str)) {
        write("Not a valid domain name.\n");
        return 1;
    }
    active_domain = str;
    ensure_domain_dirs(str);
    write("Active domain: " + str + ".\n");
    return 1;
}

int cmd_save(string str) {
    if (!may_use()) {
        return 1;
    }
    if (!active_domain || !sizeof(active_domain)) {
        write("No domain set.\n");
        return 1;
    }
    if (DOMAIN_D->save_domain(active_domain)) {
        write("Saved domain " + active_domain + ".\n");
    } else {
        write("Save failed.\n");
    }
    return 1;
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

// create <name>: write raw LPC, then load/clone/place it.
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
    path = write_path(fname, DOMAIN_ITEMS);

    if (file_size(path + ".c") != -1) rm(path + ".c");
    if (find_object(path)) destruct(find_object(path));

    body = "// made by the wand of creation\n"
        "inherit \"" ITEM_INH "\";\n"
        "void create() {\n"
        "    set_short(\"" + str + "\");\n"
        "    set_long(\"" + str + ", freshly made by the wand of creation.\\n\");\n"
        "    set_ids(" + ids_literal(ids_for_name(str)) + ");\n"
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
    string id, rest, path, body, old_short;
    object ob, env;

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
    if (living(ob)) {
        write("Cannot edit living objects.\n");
        return 1;
    }
    path = created_source_path(ob);
    if (!path) {
        write("Only created objects can be edited.\n");
        return 1;
    }
    old_short = ob->short();
    if (!old_short || old_short == "") {
        old_short = id;
    }
    env = environment(ob);

    // Real ids come from old_short (the item's own full name), not the
    // single word used to find it here: "edit key ..." on "a rusty key"
    // must not collapse its ids down to just "key", losing "rusty" and
    // the full name as ways to refer to it afterward.
    body = "// made by the wand of creation\n"
        "inherit \"" ITEM_INH "\";\n"
        "void create() {\n"
        "    set_short(\"" + old_short + "\");\n"
        "    set_long(\"" + rest + "\\n\");\n"
        "    set_ids(" + ids_literal(ids_for_name(old_short)) + ");\n"
        "    set_weight(1);\n"
        "    set_value(1);\n"
        "}\n";
    write_file(path + ".c", body, 1);

    // reload_object(ob) (this file's own original approach) does not
    // recompile: real reload_object() only re-runs the object's already
    // -compiled program from scratch, matching real FluffOS object.c
    // exactly, so it never picks up the new source just written to
    // disk. Destructing and reloading the blueprint from that file
    // (the same pattern cmd_create/cmd_room/cmd_npc already use) is
    // what actually recompiles it.
    destruct(ob);
    if (find_object(path)) {
        destruct(find_object(path));
    }
    ob = load_object(path);
    if (!ob) {
        write("Edit failed: could not compile " + path + ".c\n");
        return 1;
    }
    ob = clone_object(path);
    if (!ob) {
        write("Edit failed: could not clone " + path + ".c\n");
        return 1;
    }
    if (env && !catch(ob->move(env))) {
        write("Rewrote " + path + ".c\n");
    } else if (!catch(ob->move(this_player()))) {
        write("Rewrote " + path + ".c (moved to your inventory)\n");
    } else {
        write("Rewrote " + path + ".c but could not place it; destructing it.\n");
        destruct(ob);
    }
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
    path = write_path(fname, DOMAIN_ROOMS);
    body = "// room made by the wand of creation\n"
        "inherit \"" ROOM_INH "\";\n"
        "void create() {\n"
        "    set_exits(([]));\n"
        "    set_short(\"" + str + "\");\n"
        "    set_long(\"" + str + ".\\n\");\n"
        "    set_light(1);\n"
        "}\n";
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
    if (dest[0..0] != "/") {
        dest = write_dir(DOMAIN_ROOMS) + "/" + dest;
    }
    m = room->query_exits();
    if (!m) {
        m = ([]);
    }
    m[dir] = dest;
    room->set_exits(m);
    room->init();
    maybe_save();
    write("Exit " + dir + " now leads to " + dest + ".\n");
    return 1;
}

int cmd_npc(string str) {
    string fname, path, body;
    object ob, room;

    if (!may_use()) {
        return 1;
    }
    if (!str || !sizeof(str)) {
        write("Create which NPC? (usage: npc <name>)\n");
        return 1;
    }
    fname = replace_string(str, " ", "_");
    path = write_path(fname, DOMAIN_NPCS);

    if (file_size(path + ".c") != -1) rm(path + ".c");
    if (find_object(path)) destruct(find_object(path));

    body = "// made by the wand of creation\n"
        "inherit \"" NPC_INH "\";\n"
        "void create() {\n"
        "    npc::create();\n"
        "    set_name(\"" + str + "\");\n"
        "    set_short(\"" + str + "\");\n"
        "    set_long(\"" + str + ", a newly made NPC.\\n\");\n"
        "    set_ids(" + ids_literal(ids_for_name(str)) + ");\n"
        "}\n";
    write_file(path + ".c", body, 1);

    ob = load_object(path);
    if (!ob) {
        write("NPC compile failed: " + path + ".c\n");
        return 1;
    }
    ob = clone_object(path);
    if (!ob) {
        write("NPC clone failed: " + path + ".c\n");
        return 1;
    }

    room = environment(this_player());
    if (room && !catch(ob->move(room))) {
        write("NPC created and placed here: " + str + "\n");
    } else {
        write("NPC created but could not be placed; destructing it.\n");
        destruct(ob);
    }
    return 1;
}
