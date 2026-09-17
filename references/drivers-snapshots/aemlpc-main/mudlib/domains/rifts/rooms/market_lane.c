// room made by the wand of creation
inherit "/inherit/room";
void create() {
    object tin, hawker;

    set_exits((["east": "/domains/rifts/rooms/lower_gate"]));
    set_short("a 'Burbs market lane");
    set_long("Mud, tarp stalls, and fried soy. A slag bin is chained to a post.\n");
    set_light(1);
    // Portable tin and hawker reset on reboot; G5 does not persist them.
    tin = clone_object("/domains/rifts/items/ration_tin");
    if (tin) {
        tin->move(this_object());
    }
    hawker = clone_object("/domains/rifts/npcs/burbs_hawker");
    if (hawker) {
        hawker->move(this_object());
    }
}
