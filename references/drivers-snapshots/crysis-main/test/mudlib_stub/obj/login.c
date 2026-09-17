// Login object for the minimal test mudlib. One per connection (cloned
// by master.c's connect()). Asks for a name, clones a player object,
// binds the connection to it, and hands off. No account/password
// persistence -- any non-empty name is accepted.

void logon() {
    write("Welcome to the minimal test mudlib.\n");
    write("What name would you like? ");
    input_to("get_name");
}

void get_name(string str) {
    object player;

    if (!str || str == "") {
        write("What name would you like? ");
        input_to("get_name");
        return;
    }

    player = clone_object("/obj/user");
    player->set_name(str);
    exec(player, this_object());
    player->setup();
}
