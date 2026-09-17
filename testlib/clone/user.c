#include <globals.h>

private string name;
private int login_count;

string query_cwd() { return "/"; }
string query_name() { return name; }
void set_name(string arg) { name = arg; }
int query_login_count() { return login_count; }

void load_character(string path) {
    restore_object(path);
    login_count++;
}

void save_character() {
    if (!query_name() || query_name() == "") {
        return;
    }
    ACCOUNT_D->ensure_character_dirs(query_name());
    save_object(ACCOUNT_D->character_path(query_name()));
}

int id(string arg) {
    return arg && query_name() && arg == query_name();
}

int move(mixed dest) {
    move_object(dest);
    return 1;
}

void create() {
    login_count = 0;
}

void setup() {
    set_heart_beat(1);
#ifdef __PACKAGE_UIDS__
    seteuid(getuid(this_object()));
#endif
#ifndef __NO_WIZARDS__
    enable_wizard();
#endif
#ifndef __NO_ADD_ACTION__
    set_living_name(query_name());
    enable_commands();
    add_action("commandHook", "", 1);
#endif
}

void receive_message(string newclass, string msg) {
    receive(msg);
}

void net_dead() {
    save_character();
    set_heart_beat(0);
}

void remove() {
    save_character();
    destruct(this_object());
}

int commandHook(string arg) {
    string cmd_path = COMMAND_PREFIX + query_verb();
    object cobj = load_object(cmd_path);
    if (cobj) {
        return (int)cobj->main(arg);
    }
    return 0;
}
