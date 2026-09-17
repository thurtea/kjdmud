// Granary loft, reachable from the watch room (west) and sunken court (south).

#include <globals.h>

inherit ROOM_BASE;

void
create()
{
    set_exits((["west": ROOM_WATCH_ROOM, "south": ROOM_SUNKEN_COURT]));
}

string
short()
{
    return "the granary loft";
}

string
long()
{
    return
        "A granary loft. The room smells of spoiled grain.\n";
}

int
id(string arg)
{
    return arg == "room" || arg == "granary loft" || arg == "granary" || arg == "loft";
}

void
init()
{
    if (this_player() && interactive(this_player())) {
        write(long());
    }
    room::init();
}
