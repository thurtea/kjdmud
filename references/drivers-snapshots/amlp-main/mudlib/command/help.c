// mudlib:  library
// file:    /command/help.c
// purpose: prints the real, runnable command list for Stonewick's
//          gatehouse. Kept in exact sync with /etc/motd (the same list
//          is shown at login) -- if one changes, change the other.

#include <command.h>

int
main(string arg)
{
    write(
        "Commands you can actually run here:\n"
        "  say <text>     speak to everyone in your room\n"
        "  who            list connected players\n"
        "  eval <code>    compile and run one LPC statement, e.g.\n"
        "                 eval return 1 + 1;\n"
        "  help           show this list again\n"
        "  quit           save and disconnect\n"
        "  shutdown       stop the whole driver process\n"
        "\n"
        "Movement (each room's own description lists its real exits):\n"
        "  north / south / east / west\n"
        "\n"
        "Talking to people:\n"
        "  talk <name>               start a conversation\n"
        "  talk <name> about <topic> ask about something specific\n"
        "\n"
        "Once you are holding the reeve's rod (left on the gatehouse's\n"
        "own requisition desk, and handed to you automatically when you\n"
        "arrive):\n"
        "  clone <path>   requisition a copy of an existing design into\n"
        "                 this room or your pack\n"
        "  purge <name>   clear away an unwanted object in the room\n"
        "  create <name>  cobble together something new from raw\n"
        "                 salvage\n"
    );
    return 1;
}
