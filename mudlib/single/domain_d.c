// Domain world-graph save/restore. Graph is strings and arrays only
// (paths, not live objects) so Track A save_object needs no change.

#include <worldkit.h>

static string graph_path(string name) {
    return DOMAINS_DIR + "/" + name + "/" + DOMAIN_SAVE;
}

static string strip_dot_c(string name) {
    int n;

    if (!name) {
        return name;
    }
    n = sizeof(name);
    if (n > 2 && name[n - 2..n - 1] == ".c") {
        return name[0..n - 3];
    }
    return name;
}

static mixed *pairs_from_map(mapping m) {
    string *ks;
    mixed *out;
    int i;

    out = ({});
    if (!m) {
        return out;
    }
    ks = keys(m);
    for (i = 0; i < sizeof(ks); i++) {
        out += ({ ks[i], m[ks[i]] });
    }
    return out;
}

static mapping map_from_pairs(mixed *pairs) {
    mapping m;
    int i;

    m = ([]);
    if (!pairs) {
        return m;
    }
    for (i = 0; i + 1 < sizeof(pairs); i += 2) {
        m[pairs[i]] = pairs[i + 1];
    }
    return m;
}

static string blueprint_path(object ob) {
    string leaf, dom, kind;

    if (sscanf(file_name(ob), CREATED_DIR "/%s", leaf) == 1) {
        return CREATED_DIR "/" + leaf;
    }
    if (sscanf(file_name(ob), DOMAINS_DIR "/%s/%s/%s", dom, kind, leaf) == 3) {
        return DOMAINS_DIR "/" + dom + "/" + kind + "/" + leaf;
    }
    return file_name(ob);
}

int save_domain(string name) {
    object rec, room, *inv;
    string *files;
    mixed *rooms, *placed;
    string path, leaf;
    int i, j;

    // Master create() loads us before UIDs exist; getuid() is 0 then.
    seteuid("Root");
    if (!name || !sizeof(name)) {
        return 0;
    }
    files = get_dir(DOMAINS_DIR + "/" + name + "/" + DOMAIN_ROOMS + "/*.c");
    if (!files) {
        files = ({});
    }
    rooms = ({});
    for (i = 0; i < sizeof(files); i++) {
        leaf = strip_dot_c(files[i]);
        path = DOMAINS_DIR + "/" + name + "/" + DOMAIN_ROOMS + "/" + leaf;
        room = find_object(path);
        if (!room) {
            room = load_object(path);
        }
        if (!room) {
            continue;
        }
        placed = ({});
        inv = all_inventory(room);
        for (j = 0; j < sizeof(inv); j++) {
            if (living(inv[j])) {
                continue;
            }
            if (function_exists("query_prevent_get", inv[j]) &&
                    inv[j]->query_prevent_get()) {
                placed += ({ blueprint_path(inv[j]) });
            }
        }
        rooms += ({ ({
            path,
            pairs_from_map(room->query_exits()),
            pairs_from_map(room->query_items()),
            placed,
        }) });
    }
    rec = clone_object(DOMAIN_GRAPH);
    if (!rec) {
        return 0;
    }
    rec->set_domain(name);
    rec->set_rooms(rooms);
    rec->save_me(graph_path(name));
    destruct(rec);
    return 1;
}

int restore_domain(string name) {
    object rec, room, ob;
    mixed *rooms, *row, *scenery, *placed;
    int i, j;

    seteuid("Root");
    rec = clone_object(DOMAIN_GRAPH);
    if (!rec) {
        return 0;
    }
    if (!rec->load_me(graph_path(name))) {
        destruct(rec);
        return 0;
    }
    rooms = rec->query_rooms();
    destruct(rec);
    if (!rooms) {
        return 1;
    }
    for (i = 0; i < sizeof(rooms); i++) {
        row = rooms[i];
        if (!row || sizeof(row) < 4) {
            continue;
        }
        room = find_object(row[0]);
        if (!room) {
            room = load_object(row[0]);
        }
        if (!room) {
            continue;
        }
        room->set_exits(map_from_pairs(row[1]));
        scenery = row[2];
        if (scenery) {
            for (j = 0; j + 1 < sizeof(scenery); j += 2) {
                room->add_item(scenery[j], scenery[j + 1]);
            }
        }
        placed = row[3];
        if (placed) {
            for (j = 0; j < sizeof(placed); j++) {
                ob = clone_object(placed[j]);
                if (ob) {
                    catch(ob->move(room));
                }
            }
        }
    }
    return 1;
}

void restore_all_domains() {
    string *names;
    int i;

    names = get_dir(DOMAINS_DIR + "/*");
    if (!names) {
        return;
    }
    for (i = 0; i < sizeof(names); i++) {
        if (file_size(graph_path(names[i]) + ".o") > 0) {
            restore_domain(names[i]);
        }
    }
}

void create() {
    seteuid("Root");
    catch(restore_all_domains());
}
