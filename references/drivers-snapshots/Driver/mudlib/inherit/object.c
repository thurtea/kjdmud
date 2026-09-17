// mudlib:  library
// file:    /inherit/object.c
// purpose: theme-agnostic id / short / long / move so clones always
//          place. The reeve's rod writes new files that inherit this.

private string obj_short;
private string obj_long;
private string *obj_ids;

void set_short(string s) {
    obj_short = s;
}

void set_long(string s) {
    obj_long = s;
}

void set_ids(string *ids) {
    obj_ids = ids;
}

string query_short() {
    return obj_short;
}

string query_long() {
    return obj_long;
}

string *query_ids() {
    return obj_ids;
}

string short() {
    return obj_short;
}

string long() {
    return obj_long;
}

int id(string arg) {
    if (!arg || !obj_ids) {
        return 0;
    }
    return member_array(arg, obj_ids) != -1;
}

int move(mixed dest) {
    return move_object(dest);
}
