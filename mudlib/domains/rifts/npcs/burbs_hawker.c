// made by the wand of creation
inherit "/inherit/npc";
void create() {
    npc::create();
    set_name("burbs_hawker");
    set_short("a 'Burbs hawker");
    set_long("A wiry vendor in a patched coat, watching the lane.\n");
    set_ids(({ "burbs_hawker", "hawker" }));
}
