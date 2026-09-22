// Static room east of the gatehouse.

#include <globals.h>

inherit ROOM_BASE;

void
create()
{
    set_exits((["west": START_LOC, "north": ROOM_GRANARY_LOFT]));
}

string
short()
{
    return "the sunken court";
}

string
long()
{
    return
        "A sunken court, ankle-deep in stagnant water. Wooden stall "
        "frames stand in the water.\n";
}

int
id(string arg)
{
    return arg == "room" || arg == "sunken court" || arg == "court";
}

void
init()
{
    if (this_player() && interactive(this_player())) {
        room::show_desc();
    }
    room::init();
}
