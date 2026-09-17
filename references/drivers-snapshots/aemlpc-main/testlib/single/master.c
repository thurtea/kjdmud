#include <globals.h>

object connect(int port) {
    return new(LOGIN_OB);
}

mixed compile_object(string file) {
    return 0;
}

void preload(string file) {
    load_object(file);
}

string *epilog(int load_empty) {
    return ({ START_LOC });
}

void flag(string str) {
    write("Unknown flag.\n");
}

void log_error(string file, string message) {
    write_file(LOG_DIR + "/compile", message);
}

string get_root_uid() { return ROOT_UID; }
string get_bb_uid() { return BACKBONE_UID; }
string creator_file(string str) { return ROOT_UID; }
string domain_file(string str) { return ROOT_UID; }
string author_file(string str) { return ROOT_UID; }
string privs_file(string str) { return str; }

int valid_shadow(object ob) { return 1; }
int valid_author(string str) { return 1; }
int valid_override(string file, string name) { return 1; }
int valid_seteuid(object ob, string str) { return 1; }
int valid_domain(string str) { return 1; }
int valid_socket(object ob, string name, mixed *args) { return 1; }
int valid_write(string file, mixed caller, string fun) { return 1; }
int valid_read(string file, mixed caller, string fun) { return 1; }
