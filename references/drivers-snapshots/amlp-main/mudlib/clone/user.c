// mudlib:   library
// file:     user.c
// purpose:  is the representation of an interactive (user) in the MUD

#include <globals.h>

inherit BASE;

private string name;

// notes/ACCOUNT_LOGIN_PLAN.md build ordering item 3 (character
// persistence, 2026-08-21): the first real persisted player state,
// login_count, a plain object variable so it round-trips through
// save_object()/restore_object() the exact same way account_record.c's
// own name/hash/created/characters already do (confirmed by reading
// save_object()'s own registration comment: every object variable in
// program().objectVarNames gets serialized by name, no separate opt-in
// needed). Deliberately minimal (see globals.h's own CHARACTERS_DIR
// comment for the character-object-shape design decision this
// resolves): proves the persistence mechanism works end to end without
// inventing game mechanics this session was not scoped to design.
private int login_count;

#ifdef __INTERACTIVE_CATCH_TELL__
void catch_tell(string str) {
    receive(str);
}
#endif

// replace this with a functioning version.

string
query_cwd()
{
	return "";
}

// logon: dead code in the real login flow -- clone/login.c's own
// logon() is what the driver actually applies (master.c's connect()
// creates a /clone/login instance, not a /clone/user instance
// directly), and login.c destructs itself after cloning and setting up
// the real /clone/user object, so this function only exists as a
// leftover from before login.c was written. Left in place, banner text
// updated for consistency, matching login.c's own real banner.

void
logon()
{
	write("Stonewick's gatehouse, same as always.\n> ");
}

// query_name: called by various objects needing to know this user's name.

string
query_name()
{
	return name;
}

void
set_name(string arg)
{
//  may wish to add security to prevent just anyone from changing
//  someone else's name.
	name = arg;
}

// query_login_count/load_character/save_character: notes/
// ACCOUNT_LOGIN_PLAN.md build ordering item 3. login_count is restored
// from disk when a returning character logs back in (query_login_count()
// exists mainly so login.c can show it back to the player, real mudlib
// precedent for a lightweight "welcome back" touch).

int
query_login_count()
{
    return login_count;
}

// load_character: called by login.c's own enter_game(), once, right
// after set_name(), for every login alike (both a brand-new account's
// very first login and a returning account's Nth) -- restore_object()
// on a path with no file yet (the brand-new case) just returns 0 and
// leaves every variable at its already-initialized default, the same
// graceful no-file behavior account_record.c's own load_me() already
// relies on, so this needs no separate new-vs-returning branch here.
// Restoring also re-sets `name` from the saved file to the same value
// login.c's own set_name() call just set it to -- harmless and
// idempotent, the same redundancy account_record.c's own restore
// already accepts for its own `name` field.
void
load_character(string path)
{
    restore_object(path);
    login_count++;
}

// save_character: the one real place character state actually gets
// written to disk. Called from both remove() and net_dead() below
// (login.c never calls this directly), the two real disconnect paths
// this mudlib has -- an explicit "quit" (command/quit.c's own
// "previous_object()->remove()", BASE's own remove() otherwise just
// destructs with no save at all) and a genuine link death (net_dead(),
// the driver's own real apply, confirmed live-firing in
// src/net/Server.cpp). Guarded on a real name existing so a user object
// that never made it through login.c's own enter_game() (there is no
// other real code path that clones USER_OB) cannot write a save file
// under an empty-string bucket.
private
void
save_character()
{
    string path;

    if (!query_name() || query_name() == "") {
        return;
    }
    path = ACCOUNT_D->character_path(query_name());
    ACCOUNT_D->ensure_character_dirs(query_name());
    save_object(path);
}

// called by the present() efun (and some others) to determine whether
// an object is referred as an 'arg'.

int
id(string arg)
{
	return (arg == query_name()) || base::id(arg);
}

#ifndef __OLD_ED__
void
write_prompt() {
    switch (query_ed_mode()) {
	case 0:
	case -2:
	    write(":");
	    break;

	case -1:
	    write("> ");
	    break;

	default:
	    write("*\b");
	    break;
    }
}

void
start_ed(string file) {
    write(ed_start(file, 0));
}
#endif

#ifdef __NO_ADD_ACTION__
void
exec_command(string arg) {
    string *parts = explode(arg, " ");
    string verb = sizeof(parts) ? parts[0] : "";
    string rest = implode(parts[1..], " ");
    string cmd_path = COMMAND_PREFIX + verb;
    object cobj = load_object(cmd_path);

    if (cobj) {
	cobj->main(rest);
    } else {
	// maybe call an emote/soul daemon here
    }
}

void
process_input(string arg) {
#ifndef __OLD_ED__
    if (query_ed_mode() != -1) {
	if (arg[0] != '!') {
	    write(ed_cmd(arg));
	    return;
	}
	arg = arg[1..];
    }
#endif
    exec_command(arg);
}
#else
string
process_input(string arg)
{
#ifndef __OLD_ED__
    if (query_ed_mode() != -1) {
	if (arg[0] != '!') {
	    write(ed_cmd(arg));
	    return 0;
	}
	arg = arg[1..];
    }
#endif
    // possible to modify player input here before driver parses it.
    return arg;
}

int
commandHook(string arg)
{
    string cmd_path;
    object cobj;

    cmd_path = COMMAND_PREFIX + query_verb();

    cobj = load_object(cmd_path);
    if (cobj) {
	return (int)cobj->main(arg);
    } else {
	// maybe call an emote/soul daemon here
    }
    return 0;
}

// init: called by the driver to give the object a chance to add some
// actions (see the MudOS "applies" documentation for a better description).

void
init()
{
	// using "" as the second argument to add_action() causes the driver
	// to call commandHook() for those user inputs not matched by other
	// add_action defined commands (thus 'commandHook' becomes the default
	// action for those verbs without an explicitly associated action).
	if (this_object() == this_player()) {
		add_action("commandHook", "", 1);
	}
}
#endif

// create: called by the driver after an object is compiled.

void
create()
{
#ifdef __PACKAGE_UIDS__
    seteuid(0); // so that login.c can export uid to us
#endif
}

// receive_message: called by the message() efun.

void
receive_message(string newclass, string msg)
{
	// the meaning of 'class' is at the mudlib's discretion
	receive(msg);
}

// setup: used to configure attributes that aren't known by this_object()
// at create() time such as living_name (and so can't be done in create()).

void
setup()
{
    set_heart_beat(1);
#ifdef __PACKAGE_UIDS__
    seteuid(getuid(this_object()));
#endif
#ifndef __NO_WIZARDS__
    enable_wizard();
#endif
#ifndef __NO_ADD_ACTION__
    set_living_name(query_name());
    enable_commands();
    add_action("commandHook", "", 1);
#else
    set_this_player(this_object());
#endif
}

// net_dead: called by the gamedriver when an interactive player loses
// hir network connection to the mud.

#ifndef __NO_ENVIRONMENT__
void tell_room(object ob, string msg) {
    foreach (ob in all_inventory(ob) - ({ this_object() }))
        tell_object(ob, msg);
}
#endif

void
net_dead()
{
    save_character();
    set_heart_beat(0);
#ifndef __NO_ENVIRONMENT__
    tell_room(environment(), query_name() + " is link-dead.\n");
#endif
}

// remove: BASE's own remove() (inherit/base.c) just destructs with no
// save at all -- command/quit.c's own "previous_object()->remove()" is
// this mudlib's only other real disconnect path besides net_dead()
// above, and needed the same save added or an explicit "quit" would
// silently lose whatever this login incremented. Matches this file's
// own pre-existing "bare parent call" pattern (id() above, "base::id(
// arg)") rather than reimplementing what base::remove() already does.
void
remove()
{
    save_character();
    base::remove();
}

// reconnect: called by the login.c object when a netdead player reconnects.

void
reconnect()
{
    set_heart_beat(1);
#ifndef __NO_ENVIRONMENT__
    tell_room(environment(), "Reconnected.\n");
    tell_room(environment(), query_name() + " has reconnected.\n");
#endif
}
