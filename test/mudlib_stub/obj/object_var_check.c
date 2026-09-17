// Minimal smoke-test object for the object-variables slice (top-level
// per-object variable declarations). Reproduces the real shape found at
// secure/daemon/master.c raw lines 18-21:
//
//   static private object __Unguarded;
//   static private string __PlayerName;
//   static private object __NewPlayer;
//   static private mapping __Groups, __ReadAccess, __WriteAccess;
//
// using the same three types (object, string, mapping) and the same
// comma-separated multi-name shape on the mapping declaration. create()
// assigns into all four, the same way master.c's own create() assigns
// __Unguarded/__NewPlayer/__PlayerName to 0 before use. A second
// function, check(), is called afterward and reads the values back,
// proving object-variable state actually persists across separate calls
// on the same object instance, not just within one call's locals.

object unguarded;
string player_name;
object new_player;
mapping groups, read_access, write_access;

void create() {
    unguarded = 0;
    player_name = "nobody";
    new_player = 0;
    groups = (["wizards": ({ "thurtea" })]);
    read_access = (["/log": 1]);
    write_access = (["/save": 1]);
}

void check() {
    string result;

    result = "";

    if (player_name == "nobody") {
        result = result + "player_name ok; ";
    } else {
        result = result + "player_name wrong; ";
    }

    if (groups["wizards"][0] == "thurtea") {
        result = result + "groups ok; ";
    } else {
        result = result + "groups wrong; ";
    }

    if (read_access["/log"] == 1 && write_access["/save"] == 1) {
        result = result + "access maps ok; ";
    } else {
        result = result + "access maps wrong; ";
    }

    if (unguarded == 0 && new_player == 0) {
        result = result + "object slots ok";
    } else {
        result = result + "object slots wrong";
    }

    write("object_var_check: " + result + "\n");
}
