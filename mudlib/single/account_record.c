// file: /single/account_record.c
//
// notes/ACCOUNT_LOGIN_PLAN.md's own first build slice, "Account
// storage" (2026-08-21). One instance of this object holds exactly one
// account's own saved data: real save_object()/restore_object() always
// operate on current_object()'s own variables (no target-object
// argument), so a per-account on-disk file needs a per-account object
// to be current_object() while the efun runs, not the account_d.c
// daemon itself calling it from outside. account_d.c clones one of
// these per operation, fills or reads its variables, then destructs
// it; nothing else ever holds a live reference to one of these for
// longer than one call.
//
// Variable shape matches ACCOUNT_LOGIN_PLAN.md's own proposed
// architecture exactly: account name, crypt() hash, creation
// timestamp, and a list of character names owned by this account, kept
// as a real array from the start (even though this first slice never
// populates more than the empty default) so a later multi-character
// slice needs no save-format migration, per the plan's own explicit
// reasoning for that shape.

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

// save_me()/load_me() take the already-computed on-disk path as an
// argument rather than recomputing it here (account_d.c's own
// account_path() owns the one real bucketing rule; duplicating it here
// would be two places that have to agree instead of one).
int save_me(string path) { return save_object(path); }
int load_me(string path) { return restore_object(path); }

void create() {
    characters = ({});
}
