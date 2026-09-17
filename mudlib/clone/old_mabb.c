// NPC in the watch room. Talk via "talk mabb" or "talk mabb about <topic>".

// call_other ("->") does not fall back to move_object(); rooms call mabb->move().
int move(mixed dest) {
    return move_object(dest);
}

string *mabb_ids;

void create() {
    mabb_ids = ({ "mabb", "old mabb", "woman" });
}

int id(string arg) {
    return arg && member_array(arg, mabb_ids) != -1;
}

string short() {
    return "Old Mabb";
}

string long() {
    return "An old woman in the watch room.\n";
}

void init() {
    add_action("cmd_talk", "talk");
}

int cmd_talk(string arg) {
    string target, topic;
    int idx;

    if (!arg || !sizeof(arg)) {
        write("Talk to whom?\n");
        return 1;
    }

    idx = strsrch(arg, " about ");
    if (idx == -1) {
        target = arg;
        topic = 0;
    } else {
        target = arg[0..idx - 1];
        topic = arg[idx + 7..];
    }

    if (!id(target)) {
        return 0;
    }

    if (!topic || !sizeof(topic)) {
        write("Old Mabb looks up. \"Yes?\"\n");
        return 1;
    }

    if (strsrch(topic, "tower") != -1 || strsrch(topic, "gatehouse") != -1) {
        write("\"The gatehouse is downstairs. This is the watch room.\"\n");
        return 1;
    }

    if (strsrch(topic, "wand") != -1 || strsrch(topic, "creation") != -1
        || strsrch(topic, "rod") != -1) {
        write("\"The wand of creation is on the worktable downstairs.\"\n");
        return 1;
    }

    write("She shrugs. \"Can't help you with that one.\"\n");
    return 1;
}
