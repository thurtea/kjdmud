#include <command.h>

int
main(string arg)
{
    object look;

    look = load_object(COMMAND_PREFIX "look");
    if (!look) {
        write("You cannot examine anything right now.\n");
        return 1;
    }
    return (int)look->main(arg);
}
