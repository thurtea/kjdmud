#include <command.h>

int
main(string arg)
{
    object inventory;

    inventory = load_object(COMMAND_PREFIX "inventory");
    if (!inventory) {
        write("You cannot check your inventory right now.\n");
        return 1;
    }
    return (int)inventory->main(arg);
}
