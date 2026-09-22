#include <command.h>

int
main(string arg)
{
    object env, ob, *inv;
    mapping items;
    string desc;
    int i;

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
        if (function_exists("exits_line", env)) {
            write(env->exits_line());
        }
        inv = all_inventory(env);
        desc = "";
        for (i = 0; i < sizeof(inv); i++) {
            if (inv[i] == this_player()) {
                continue;
            }
            if (inv[i]->short() && inv[i]->short() != "") {
                desc += inv[i]->short() + "\n";
            }
        }
        if (desc != "") {
            write("Contents:\n" + desc);
        }
        return 1;
    }

    ob = present(arg, this_player());
    if (!ob && env) {
        ob = present(arg, env);
    }
    if (!ob) {
        if (env && function_exists("query_items", env)) {
            items = env->query_items();
            if (items && items[arg]) {
                write(items[arg]);
                return 1;
            }
        }
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
