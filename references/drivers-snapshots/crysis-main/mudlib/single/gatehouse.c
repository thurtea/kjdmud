// mudlib:  library
// file:    /single/gatehouse.c
// purpose: the starting area -- a real place, not a "test entrance
//          hall." This is the gatehouse at the edge of Stonewick, an old
//          settlement half-collapsed since the Long Burn swept its
//          granary district some thirty-odd years back (a dry-summer
//          fire that got away from whoever was tending the grain store,
//          nothing more exotic than that -- killed enough people that
//          the garrison never recovered its numbers, and within a few
//          years everyone who could leave had). What's left is picked
//          over by scavengers, deserters, and whatever's since moved
//          into the flooded cellars and rotten granary lofts.
//
//          Locals and scavengers alike call this building the Ashgate,
//          for the soot still caked into its stones. One of four static
//          rooms this mudlib has (see ROOM_WATCH_ROOM/ROOM_SUNKEN_COURT/
//          ROOM_GRANARY_LOFT in globals.h and each of their own files):
//          a small 2x2 layout, gatehouse <-> watch room (north/south)
//          and gatehouse <-> sunken court (east/west), watch room <->
//          granary loft (east/west) and sunken court <-> granary loft
//          (north/south).
//
// init() still writes the room description on arrival. look/examine
// now exist as /command/look.c and /command/examine.c, so a player can
// also ask again after they are already standing here.

#include <globals.h>

inherit ROOM_BASE;

void
create()
{
    set_exits((["north": ROOM_WATCH_ROOM, "east": ROOM_SUNKEN_COURT]));
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
        "The old gatehouse they call the Ashgate, named for the soot "
        "still worked into its stones from the Long Burn thirty-odd "
        "years back. The gate itself rotted off its hinges long ago; "
        "what's left is a squat stone room, a cold hearth, and a "
        "dusty worktable -- a wand of creation still rests on it, "
        "waiting for someone with a reason to pick "
        "it up. A half-collapsed stair leads up into the old watch "
        "room; a gap in the east wall opens onto what used to be the "
        "market square. Exits: " + exits_desc() + ".\n"
        "Type 'help' for the full list of runnable commands.\n";
}

int
id(string arg)
{
    return arg == "room" || arg == "gatehouse" || arg == "ashgate" || arg == "gate";
}

// init() fires on every entry. The wand hand-out is guarded so a
// returning player does not get a second one. The wand is moved into
// the room first, then the player, so its own init() (add_action) runs
// while a commands-enabled occupant is present.
void
init()
{
    object player;
    object wand;

    player = this_player();
    if (!player) {
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
