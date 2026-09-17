// room made by the wand of creation
inherit "/inherit/room";
void create() {
    set_exits((["south": "/domains/rifts/rooms/lower_gate"]));
    set_short("a 'Burbs watch post");
    set_long("A riveted tower with a dead floodlight. Chalk marks count patrols on the inner wall.\n");
    set_light(1);
    add_item("floodlight", "A dead floodlight, rusted to the rail. It will not come on.\n");
}
