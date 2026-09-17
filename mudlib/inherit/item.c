// mudlib:  library
// file:    /inherit/item.c
// purpose: weight, value, and prevent_get over /inherit/object. No
//          create() of its own; unset fields read back 0.

inherit "/inherit/object";

private int item_weight;
private int item_value;
private int prevent_get;

void set_weight(int n) {
    item_weight = n;
}

void set_value(int n) {
    item_value = n;
}

void set_prevent_get(int n) {
    prevent_get = n;
}

int query_weight() {
    return item_weight;
}

int query_value() {
    return item_value;
}

int query_prevent_get() {
    return prevent_get;
}
