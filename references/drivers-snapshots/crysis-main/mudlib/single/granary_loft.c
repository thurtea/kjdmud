// mudlib:  library
// file:    /single/granary_loft.c
// purpose: one of three static rooms reachable from the gatehouse
//          (/single/gatehouse.c) -- see that file's own header comment
//          for the full setting and the 2x2 layout this is part of.
//          Reachable two ways (the rope bridge from the watch room, or
//          the ladder from the sunken court), closing the loop.

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
        "The old grain store, three stories up, still smells of rot "
        "even after thirty dry years. Whatever took root in the spoiled "
        "grain has spread through the floorboards since, pale and "
        "faintly damp to the touch. Old Mabb's warnings about this "
        "place did not feel like exaggeration. Exits: " + exits_desc() + ".\n";
}

int
id(string arg)
{
    return arg == "room" || arg == "granary loft" || arg == "granary" || arg == "loft" || arg == "grain store";
}

// No "look" command exists anywhere in this mudlib (see gatehouse.c's
// own header comment), so init() writes the room's own description to
// whoever just arrived, the same moment a "look" would have.
void
init()
{
    if (this_player()) {
        write(long());
    }
    room::init();
}
