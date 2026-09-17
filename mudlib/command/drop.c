#include <command.h>

int
main(string arg)
{
    object env, ob;
    string desc;

    if (!arg || arg == "") {
        write("Drop what?\n");
        return 1;
    }

    ob = present(arg, this_player());
    if (!ob) {
        write("You are not carrying that.\n");
        return 1;
    }

    env = environment(this_player());
    if (!env) {
        write("You are nowhere.\n");
        return 1;
    }

    if (catch(ob->move(env))) {
        write("You cannot drop that.\n");
        return 1;
    }

    desc = ob->short();
    if (!desc || desc == "") {
        desc = arg;
    }
    write("You drop " + desc + ".\n");
    return 1;
}
