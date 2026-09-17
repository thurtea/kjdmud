#include <command.h>

int
main(string arg)
{
    object take;

    take = load_object(COMMAND_PREFIX "take");
    if (!take) {
        write("You cannot take anything right now.\n");
        return 1;
    }
    return (int)take->main(arg);
}
