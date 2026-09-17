// mudlib:  library
// file:    /clone/old_mabb.c
// purpose: a real NPC, not scenery -- a scavenger squatting in the
//          gatehouse's old watch room (/single/watch_room.c, which
//          clones and places exactly one of her, once, at its own
//          create() time). Talks via a real add_action-driven "talk"
//          command, "talk mabb" or "talk mabb about <topic>", not a
//          static block of description text.
//
// move(): matches wand_of_creation.c's own documented move() wrapper
// (see that file's own comment for the full explanation) -- this
// driver's call_other ("->") never falls back to the move_object() efun,
// so watch_room.c's own "mabb->move(this_object())" needs Mabb to define
// this herself, the same way every other placeable object in this
// mudlib does.
int move(mixed dest) {
    return move_object(dest);
}

string *mabb_ids;

void create() {
    mabb_ids = ({ "mabb", "old mabb", "scavenger", "woman" });
}

int id(string arg) {
    return arg && member_array(arg, mabb_ids) != -1;
}

string short() {
    return "Old Mabb";
}

string long() {
    return
        "A wiry old woman with a scavenger's eye for anything worth "
        "carrying, camped out in what used to be the gatehouse's watch "
        "room. She doesn't look like she trusts you, but she doesn't "
        "look like she's about to do anything about it either.\n";
}

// init: registers "talk" on whoever just walked into her room (see
// VM::moveObject()'s own "Leg 2" -- a room's existing occupant's init()
// fires on the incoming player exactly this way, the same mechanism
// wand_of_creation.c's own held-item hand-out already relies on, just
// triggered from the opposite direction: an existing occupant reacting
// to a newcomer instead of a newly-moved item reacting to its new
// surroundings). Re-registered every time a player arrives, matching
// wand_of_creation.c's own precedent of calling add_action()
// unconditionally rather than guarding against a duplicate registration.
void init() {
    add_action("cmd_talk", "talk");
}

// talk <name> [about <topic>] -- a short, real conversation, not a
// static description. Anyone typing "talk" to someone other than Mabb
// falls through (returns 0) rather than claiming the command, the same
// convention this driver's own add_action dispatch already expects (see
// VM::dispatchCommand()'s own "first matching handler that returns
// truthy" comment).
int cmd_talk(string arg) {
    string target, topic;
    int idx;

    if (!arg || !sizeof(arg)) {
        write("Talk to whom?\n");
        return 1;
    }

    idx = strsrch(arg, " about ");
    if (idx == -1) {
        target = arg;
        topic = 0;
    } else {
        target = arg[0..idx - 1];
        topic = arg[idx + 7..];
    }

    if (!id(target)) {
        return 0;
    }

    if (!topic || !sizeof(topic)) {
        write("Old Mabb looks up from her fire. \"Didn't expect company. "
            "Long as you're not here for my salvage, I don't much care "
            "why you're here.\"\n");
        return 1;
    }

    if (strsrch(topic, "tower") != -1 || strsrch(topic, "gatehouse") != -1
        || strsrch(topic, "ashgate") != -1) {
        write("\"This place? Held the east wall before the Long Burn took "
            "the granary district. Wasn't much of a garrison left after "
            "that, and less of a reason to stay.\"\n");
        return 1;
    }

    if (strsrch(topic, "rod") != -1 || strsrch(topic, "reeve") != -1
        || strsrch(topic, "wand") != -1) {
        write("\"That old rod on the desk downstairs? Belonged to the "
            "reeve who kept the stores. Requisitioned lumber, tools, "
            "whatever the garrison needed. Nobody's claimed it in years "
            "-- reckon it's yours now if you're fool enough to carry "
            "it.\"\n");
        return 1;
    }

    if (strsrch(topic, "rot") != -1 || strsrch(topic, "granary") != -1
        || strsrch(topic, "mold") != -1 || strsrch(topic, "monster") != -1) {
        write("\"Don't go poking around the granary loft without a light "
            "and a reason. Something's grown in the grain-rot down "
            "there, and it doesn't like company either.\"\n");
        return 1;
    }

    if (strsrch(topic, "fire") != -1 || strsrch(topic, "burn") != -1) {
        write("\"Thirty-some years back now. Dry summer, a spark in the "
            "grain store, and half of Stonewick went up before anyone "
            "got water on it. Wasn't magic, wasn't a curse -- just bad "
            "luck and dry timber.\"\n");
        return 1;
    }

    write("She shrugs. \"Can't help you with that one.\"\n");
    return 1;
}
