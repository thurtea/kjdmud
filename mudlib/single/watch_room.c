// Watch room north of the gatehouse. Places Old Mabb once at create().

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
        "A watch room above the gatehouse. Old Mabb is here, with a "
        "bedroll, a cookfire, and a pile of gear.\n";
}

int
id(string arg)
{
    return arg == "room" || arg == "watch room" || arg == "watchroom";
}

void
init()
{
    if (this_player() && interactive(this_player())) {
        room::show_desc();
    }
    room::init();
}
