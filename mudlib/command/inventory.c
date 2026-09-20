#include <command.h>

int
main(string arg)
{
    object *inv;
    string desc;
    int i;

    inv = all_inventory(this_player());
    desc = "";
    for (i = 0; i < sizeof(inv); i++) {
        if (inv[i]->short() && inv[i]->short() != "") {
            desc += inv[i]->short() + "\n";
        }
    }
    if (desc == "") {
        write("You are not carrying anything.\n");
        return 1;
    }
    write("You are carrying:\n" + desc);
    return 1;
}
