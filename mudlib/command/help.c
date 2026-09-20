// Prints the runnable command list. Keep in sync with /etc/motd.

#include <command.h>

int
main(string arg)
{
    write(
        "Commands you can actually run here:\n"
        "  look [id]      look at the room, or at something here\n"
        "  examine [id]   same as look\n"
        "  take <id>      pick up something in the room\n"
        "  get <id>       same as take\n"
        "  drop <id>      put down something you are carrying\n"
        "  inventory      list what you are carrying\n"
        "  inv            same as inventory\n"
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
        "Once you are holding the wand of creation (left on the\n"
        "gatehouse worktable, and handed to you automatically when you\n"
        "arrive):\n"
        "  clone <path>   clone a copy of an existing object into this\n"
        "                 room or your pack\n"
        "  purge <name>   clear away an unwanted object in the room\n"
        "  create <name>  build something new\n"
        "  npc <name>     create a living NPC in this room\n"
        "  edit <id> <text> rewrite a created object's description\n"
        "  room <name>    write a new room (domain or /data/created/)\n"
        "  exit <dir> <path> link this room to another\n"
        "  domain [name]  set or show the active domain (none clears)\n"
        "  save           write the active domain world graph\n"
    );
    return 1;
}
