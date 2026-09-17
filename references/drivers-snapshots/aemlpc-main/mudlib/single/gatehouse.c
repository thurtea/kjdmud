// Starting room. Hands out the wand of creation once per player on entry.

#include <globals.h>

inherit ROOM_BASE;

void
create()
{
    set_exits((["north": ROOM_WATCH_ROOM, "east": ROOM_SUNKEN_COURT,
        "west": RIFTS_LOWER_GATE]));
}

string
short()
{
    return "the gatehouse";
}

string
long()
{
    return
        "A squat stone room with a cold hearth and a dusty worktable. "
        "A wand of creation rests on the table. A stair leads north to "
        "the watch room. An opening in the east wall leads to the "
        "sunken court. A west gap opens toward the Chi-Town 'Burbs.\n"
        "Type 'help' for the full list of runnable commands.\n";
}

int
id(string arg)
{
    return arg == "room" || arg == "gatehouse" || arg == "gate";
}

// Wand hand-out is guarded so a returning player does not get a second one.
void
init()
{
    object player;
    object wand;

    player = this_player();
    if (!player || !interactive(player)) {
        return;
    }

    write(long());

    if (!present("wand", player)) {
        wand = clone_object(WAND_OB);
        if (wand) {
            wand->move(this_object());
            wand->move(player);
        }
    }

    room::init();
}
