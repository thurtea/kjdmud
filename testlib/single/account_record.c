string name;
string hash;
int created;
string *characters;

void set_name(string n) { name = n; }
void set_hash(string h) { hash = h; }
void set_created(int t) { created = t; }
void set_characters(string *c) { characters = c; }

string query_name() { return name; }
string query_hash() { return hash; }
int query_created() { return created; }
string *query_characters() { return characters; }

int save_me(string path) { return save_object(path); }
int load_me(string path) { return restore_object(path); }

void create() {
    characters = ({});
}
