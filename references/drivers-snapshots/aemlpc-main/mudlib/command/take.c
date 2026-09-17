#include <command.h>

int
main(string arg)
{
    object env, ob;
    string desc;

    if (!arg || arg == "") {
        write("Take what?\n");
        return 1;
    }

    env = environment(this_player());
    if (!env) {
        write("You are nowhere.\n");
        return 1;
    }

    ob = present(arg, env);
    if (!ob) {
        write("You do not see that here.\n");
        return 1;
    }

    if (ob->query_prevent_get()) {
        write("You cannot take that.\n");
        return 1;
    }

    if (catch(ob->move(this_player()))) {
        write("You cannot take that.\n");
        return 1;
    }

    desc = ob->short();
    if (!desc || desc == "") {
        desc = arg;
    }
    write("You take " + desc + ".\n");
    return 1;
}
