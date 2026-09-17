// mudlib:  library
// file:    /inherit/item.c
// purpose: weight and value accessors over /inherit/object, for objects
//          the wand of creation makes. No create() of its own; unset
//          fields read back 0.

inherit "/inherit/object";

private int item_weight;
private int item_value;

void set_weight(int n) {
    item_weight = n;
}

void set_value(int n) {
    item_value = n;
}

int query_weight() {
    return item_weight;
}

int query_value() {
    return item_value;
}
