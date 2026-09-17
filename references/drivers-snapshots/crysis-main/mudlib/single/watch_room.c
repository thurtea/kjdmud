// mudlib:  library
// file:    /single/watch_room.c
// purpose: one of three static rooms reachable from the gatehouse
//          (/single/gatehouse.c) -- see that file's own header comment
//          for the full setting and the 2x2 layout this is part of.
//          Up the half-collapsed stair from the Ashgate; a scavenger
//          called Old Mabb has made a camp of it. Her own dialogue lives
//          in /clone/old_mabb.c, moved in here once, at this room's own
//          create() time, the same way a genuinely static fixture would
//          be -- not re-cloned per visitor.

#include <globals.h>

inherit ROOM_BASE;

void
create()
{
    object mabb;

    set_exits((["south": START_LOC, "east": ROOM_GRANARY_LOFT]));

    mabb = clone_object(NPC_OLD_MABB);
    if (mabb) {
        mabb->move(this_object());
    }
}

string
short()
{
    return "the watch room";
}

string
long()
{
    return
        "Up a half-collapsed stair from the Ashgate, the old watch room "
        "still has most of its roof. A scavenger everyone calls Old "
        "Mabb has made a camp of it -- a bedroll, a cookfire, a heap of "
        "salvage she's in no hurry to share. A rope-and-plank bridge "
        "someone rigged years ago sags off to the east, toward the old "
        "granary. Exits: " + exits_desc() + ".\n";
}

int
id(string arg)
{
    return arg == "room" || arg == "watch room" || arg == "watchroom";
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
