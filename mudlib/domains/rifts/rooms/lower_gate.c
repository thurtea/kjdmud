// room made by the wand of creation
inherit "/inherit/room";
void create() {
    set_exits((["east": "/single/gatehouse",
        "west": "/domains/rifts/rooms/market_lane",
        "north": "/domains/rifts/rooms/watch_post"]));
    set_short("the lower 'Burbs gate");
    set_long("A scrap-metal gate into the Chi-Town 'Burbs. Welded car hoods make the walls. A Coalition checkpoint sits under a patched sign.\n");
    set_light(1);
    add_item("poster", "A peeling poster: REPORT UNREGISTERED TALENT. Reward posted in credits.\n");
}
