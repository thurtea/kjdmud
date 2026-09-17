// Minimal smoke-test object for the range/slice indexing slice
// (str[start..end] syntax). create() reproduces the real shape found
// at all seven confirmed sites in secure/daemon/master.c's user-save
// path helpers, the single-character-prefix sharding idiom:
//
//   DIR_USERS+"/"+name[0..0]
//
// DIR_USERS is a config.h macro in the real mudlib; this stub inlines
// its real value directly rather than depending on config.h, the same
// way arithmetic_check.c and guard_char_check.c avoid pulling in real
// mudlib headers for an unrelated smoke test.

void create() {
    string dir_users;
    string name;
    string path;

    dir_users = "/secure/save/users";
    name = "thurtea";
    path = dir_users+"/"+name[0..0]+"/"+name+".o";

    if (path == "/secure/save/users/t/thurtea.o") {
        write("shard ok: " + path + "\n");
    } else {
        write("shard wrong: " + path + "\n");
    }
}
