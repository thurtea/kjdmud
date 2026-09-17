#include <globals.h>

string short() {
    return "the testlib start room";
}

string long() {
    return
        "You are in a plain test room for the driver.\n"
        "This mudlib exists to test connect, login, accounts,\n"
        "characters, and a few basic commands.\n";
}

void init() {
}

int move(mixed dest) {
    return move_object(dest);
}

int id(string arg) {
    return arg && (arg == "room" || arg == "start" || arg == "test room");
}
