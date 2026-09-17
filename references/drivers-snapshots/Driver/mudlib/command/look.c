#include <command.h>

int
main(string arg)
{
    object env, ob;
    string desc;

    env = environment(this_player());
    if (!arg || arg == "") {
        if (!env) {
            write("You are nowhere.\n");
            return 1;
        }
        desc = env->long();
        if (!desc || desc == "") {
            desc = env->short();
        }
        if (!desc || desc == "") {
            write("You see nothing special.\n");
            return 1;
        }
        write(desc);
        return 1;
    }

    ob = present(arg, this_player());
    if (!ob && env) {
        ob = present(arg, env);
    }
    if (!ob) {
        write("You do not see that here.\n");
        return 1;
    }
    desc = ob->long();
    if (!desc || desc == "") {
        desc = ob->short();
    }
    if (!desc || desc == "") {
        write("You see nothing special.\n");
        return 1;
    }
    write(desc);
    return 1;
}
