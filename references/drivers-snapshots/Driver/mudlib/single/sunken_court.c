// mudlib:  library
// file:    /single/sunken_court.c
// purpose: one of three static rooms reachable from the gatehouse
//          (/single/gatehouse.c) -- see that file's own header comment
//          for the full setting and the 2x2 layout this is part of.

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
        "What used to be Stonewick's market square sits ankle-deep in "
        "stagnant water -- the old cistern cracked sometime after the "
        "Long Burn and nobody left who could fix it. The stalls have "
        "rotted down to their frames; a few still have goods fused to "
        "the wood by decades of damp. A leaning ladder climbs north "
        "toward the granary loft. Exits: " + exits_desc() + ".\n";
}

int
id(string arg)
{
    return arg == "room" || arg == "sunken court" || arg == "court" || arg == "market" || arg == "square";
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
