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
// No "look" command exists anywhere in this mudlib (stock Lil never
// shipped one either -- see WAND_OF_CREATION_SCOPING.md's own note on
// this exact point), so init() below writes the room's own description
// straight to whoever just arrived, the same moment a "look" command
// would have, rather than leaving it unreachable.

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
        "requisition desk nobody's touched in years -- a reeve's rod "
        "still rests on it, waiting for someone with a reason to pick "
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

// init: fires every time a living moves into this room, not just the
// first time (a real MudOS/FluffOS apply, already recognized and fired
// by this driver -- see ApplyTable.cpp) -- so the rod hand-out is
// guarded against handing out a second one to someone who already has
// one (walks out and back in, reconnects, etc.). The description write
// is unguarded -- showing it again on re-entry is exactly what a real
// "look" would do too.
//
// The handed-out rod is moved here in two steps, not one, matching
// wand_of_creation.c's own documented mechanism (see its own comment on
// held()/init()): this driver's VM::moveObject() only calls init() on
// the *moved* object (which is where the rod's own add_action calls
// live) when the destination's existing occupants already have
// commandsEnabled() true (true here because the player is already a
// genuine occupant of this room by the time this function runs, not
// true of the player's own (empty) inventory. Moving the rod straight
// into the player with a single move() call would leave its add_action
// calls never registered -- confirmed live the first time this was
// tried, all three rod commands silently fell through to commandHook()
// and failed with "source file not found".
//
// room::init() (a bare qualified parent call) registers this room's own
// exit verbs (north/east here) onto the arriving player, exactly the
// same "runs alongside, not instead of" pattern this file already used
// before room.c existed.
void
init()
{
    object player;
    object rod;

    player = this_player();
    if (!player) {
        return;
    }

    write(long());

    if (!present("rod", player)) {
        rod = clone_object(REEVES_ROD_OB);
        if (rod) {
            rod->move(this_object()); // enters the room the player already occupies -- fires the rod's own init()
            rod->move(player);        // now genuinely held
        }
    }

    room::init();
}
