// mudlib:  library
// file:    /inherit/npc.c
// purpose: living object with a name, zone-path wander, and catch_tell.
//          Generated files call npc::create() so living/heart_beat stick.

inherit "/inherit/object";

private string npc_name;
private string *move_zone;
private string last_heard;

void set_name(string n) {
    npc_name = n;
    if (n && n != "") {
        set_living_name(n);
    }
}

string query_name() {
    return npc_name;
}

void set_move_zone(string *paths) {
    move_zone = paths;
}

string *query_move_zone() {
    return move_zone;
}

string query_last_heard() {
    return last_heard;
}

void catch_tell(string msg) {
    last_heard = msg;
}

void receive_message(string cl, string msg) {
    last_heard = msg;
}

void create() {
    enable_commands();
    set_heart_beat(1);
}

void heart_beat() {
    object env, dest;
    mapping exits;
    string *dirs, *legal, dir, path;
    int i;

    if (!move_zone || !sizeof(move_zone)) {
        return;
    }
    env = environment();
    if (!env) {
        return;
    }
    exits = env->query_exits();
    if (!exits) {
        return;
    }
    dirs = keys(exits);
    legal = ({});
    for (i = 0; i < sizeof(dirs); i++) {
        path = exits[dirs[i]];
        if (member_array(path, move_zone) != -1) {
            legal += ({ dirs[i] });
        }
    }
    if (!sizeof(legal)) {
        return;
    }
    dir = legal[random(sizeof(legal))];
    dest = find_object(exits[dir]);
    if (!dest) {
        dest = load_object(exits[dir]);
    }
    if (dest) {
        move(dest);
    }
}
