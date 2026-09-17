// One domain's world graph. save_object/restore_object run on this
// object's own variables, same pattern as /single/account_record.c.

string domain;
mixed *rooms;

void set_domain(string n) { domain = n; }
void set_rooms(mixed *r) { rooms = r; }

string query_domain() { return domain; }
mixed *query_rooms() { return rooms; }

int save_me(string path) { return save_object(path); }
int load_me(string path) { return restore_object(path); }

void create() {
    rooms = ({});
}
