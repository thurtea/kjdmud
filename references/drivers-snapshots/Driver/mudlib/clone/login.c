#include <globals.h>

// notes/ACCOUNT_LOGIN_PLAN.md build ordering item 2 (login integration,
// 2026-08-21): a real input_to() state machine wired to ACCOUNT_D
// (/single/account_d.c, item 1, done earlier the same day), replacing
// the previous unconditional clone-and-exec() with no auth at all (this
// file's own former top-of-file comment, "needs fixed to handle
// passwords", is what this closes). Structurally modeled on
// temp/core-lib/secure/login.c's own logon() -> getUserLogin() ->
// enterPassword()/createUser() shape, already read and cited in that
// plan's own reference-corpora survey, but calling ACCOUNT_D directly
// rather than a separate authenticationService daemon (this plan's own
// deliberately smaller first slice).
//
// Character concept (that plan's own item 3): resolved 2026-08-21, same
// day as this file's own login integration -- see globals.h's own
// CHARACTERS_DIR comment for the "merge, not a separate object"
// decision, and /clone/user.c's own load_character()/save_character()
// for the actual persistence.
//
// Character creation (that plan's own item 4, 2026-08-21, later the
// same week): a brand-new account's player now names their own
// character explicitly (`got_character_name()` below), a real, distinct
// name from the account name, recorded into `account_d`'s own
// `characters()` list (built in item 1, never populated until now). A
// returning account picks up its own first character from that same
// list. Pre-item-4 accounts (created by an earlier session's own
// account-name-as-character-name shape) still have an empty
// `characters` list on disk -- `got_login_password()` below falls back
// to the account name itself for exactly those, so an account created
// before this item keeps loading its own already-existing character
// file correctly, not a new, empty one under a name nobody ever chose.
//
// Character selection (that plan's own item 5, 2026-08-21, later still
// the same week): if `characters()` returns more than one name,
// `got_login_password()` below now shows a numbered menu
// (`got_character_selection()`) instead of silently defaulting to
// `characters[0]`. This plan's own "Proposed architecture" section for
// this item also describes "list existing characters, pick one or
// create new" -- only the "pick one" half is built this slice.
// "Create new" from an *existing, already-authenticated* account is a
// genuinely separate, larger piece (a new login-flow branch reachable
// after selection, not before it, plus deciding what happens to an
// account already past `MAX_LOGIN_TRIES`-style abuse concerns for
// repeated character creation) deliberately not attempted here, noted
// separately rather than forced -- and, structurally, nothing before
// this item ever gives an account more than one character to select
// among in real play either way (`add_character()` is still only ever
// called once, from `got_character_name()` below, itself only reachable
// from brand-new-account creation), so this slice's own menu is real,
// correct, and tested, but not yet reachable through ordinary play
// without that follow-on piece -- the same honest gap this plan's own
// per-item writeups have flagged before rather than glossed over.

private string account_name;
private string character_name;
private string *pending_characters;
private int tries;
private string pending_password;

#ifdef __INTERACTIVE_CATCH_TELL__
void catch_tell(string str) {
    receive(str);
}
#endif

// Renamed from valid_account_name() (item 2): the check itself was
// always generic string-path-safety validation, not account-specific,
// and item 4 below needs the identical check for a character name too
// -- reusing it under its real, general name rather than duplicating it
// under a second, character-specific one.
private
int
valid_name(string name)
{
    // Kept deliberately simple: real corpora reject far more (reserved
    // words, profanity lists, length caps) but none of that is a driver
    // gap or an account_d.c contract this slice needs to satisfy --
    // only requirement account_d.c's own account_path()/character_path()
    // actually has is "no '/' in the name" (it is used unescaped as a
    // path segment).
    if (!name || name == "") return 0;
    if (strsrch(name, "/") != -1) return 0;
    return 1;
}

private
void
enter_game()
{
    object user;

    write("\n");
#ifdef __PACKAGE_UIDS__
    seteuid(getuid(this_object()));
#endif
    user = new(USER_OB);
    user->set_name(character_name);
    // notes/ACCOUNT_LOGIN_PLAN.md build ordering item 3: restores this
    // character's own persisted state (currently just login_count) if a
    // save file already exists (a returning character), or leaves every
    // variable at its just-initialized default and starts counting from
    // this login if not (a brand-new character, right after
    // got_character_name() below) -- load_character() itself handles
    // both cases identically, no branch needed here.
    user->load_character(ACCOUNT_D->character_path(character_name));
    write("Welcome back! You have logged in " + user->query_login_count() +
        " time(s).\n");
    exec(user, this_object());
    user->setup();
#ifndef __NO_ENVIRONMENT__
    user->move(START_LOC);
#endif
    destruct(this_object());
}

void
login_timeout()
{
    write("\nTimed out waiting for input. Goodbye.\n");
    destruct(this_object());
}

void
logon()
{
#ifdef __NO_ADD_ACTION__
    set_this_player(this_object());
#endif
    cat("/etc/motd");
    write("\nWhat account name do you wish? ");
#ifdef __PACKAGE_UIDS__
    seteuid(getuid(this_object()));
#endif
    tries = 0;
    call_out("login_timeout", LOGIN_TIMEOUT_SECS);
    input_to("got_account_name");
}

void
got_account_name(string str)
{
    remove_call_out("login_timeout");

    if (!valid_name(str)) {
        write("\nThat is not a valid account name. What account name do you wish? ");
        call_out("login_timeout", LOGIN_TIMEOUT_SECS);
        input_to("got_account_name");
        return;
    }

    account_name = str;
    call_out("login_timeout", LOGIN_TIMEOUT_SECS);
    if (ACCOUNT_D->account_exists(account_name)) {
        write("\nPassword: ");
        input_to("got_login_password", INPUT_NOECHO);
    } else {
        write("\nNo such account. Creating a new account named '" +
            account_name + "'.\n");
        write("Please choose a password of at least " + MIN_PASSWORD_LEN +
            " letters: ");
        input_to("got_new_password", INPUT_NOECHO);
    }
}

void
got_login_password(string str)
{
    string *chars;

    remove_call_out("login_timeout");

    if (ACCOUNT_D->check_password(account_name, str)) {
        // notes/ACCOUNT_LOGIN_PLAN.md build ordering item 4: a returning
        // account's own real character list, per this file's own header
        // comment above -- an empty list (this account predates item 4)
        // falls back to the account name itself, the exact shape items
        // 2/3 already used and already has a real character file saved
        // under it.
        chars = ACCOUNT_D->characters(account_name);
        if (sizeof(chars) > 1) {
            // Item 5: more than one real character, show the menu
            // instead of silently defaulting to chars[0].
            show_character_menu(chars);
            return;
        }
        character_name = sizeof(chars) ? chars[0] : account_name;
        enter_game();
        return;
    }

    tries++;
    if (tries >= MAX_LOGIN_TRIES) {
        write("\nToo many failed attempts. Goodbye.\n");
        destruct(this_object());
        return;
    }

    write("\nIncorrect password. Password: ");
    call_out("login_timeout", LOGIN_TIMEOUT_SECS);
    input_to("got_login_password", INPUT_NOECHO);
}

// show_character_menu()/got_character_selection(): notes/
// ACCOUNT_LOGIN_PLAN.md build ordering item 5 (character selection,
// 2026-08-21). chars is stashed in pending_characters so
// got_character_selection() below can resolve a chosen number back to
// a real name without re-querying account_d (the account's own real
// character list cannot change mid-selection, nothing else touches it
// during this same login attempt).
private
void
show_character_menu(string *chars)
{
    int i;

    pending_characters = chars;
    write("\nChoose a character:\n");
    for (i = 0; i < sizeof(chars); i++) {
        write("  " + (i + 1) + ". " + chars[i] + "\n");
    }
    write("Character number: ");
    call_out("login_timeout", LOGIN_TIMEOUT_SECS);
    input_to("got_character_selection");
}

void
got_character_selection(string str)
{
    int choice;

    remove_call_out("login_timeout");

    // sscanf()'s own real "did this actually match" contract: a return
    // of 1 means the whole pattern (a plain %d) matched end to end, not
    // just a numeric prefix of a longer, partly-garbage string.
    if (!str || sscanf(str, "%d", choice) != 1 ||
            choice < 1 || choice > sizeof(pending_characters)) {
        write("\nNot a valid choice.");
        show_character_menu(pending_characters);
        return;
    }

    character_name = pending_characters[choice - 1];
    pending_characters = 0;
    enter_game();
}

void
got_new_password(string str)
{
    remove_call_out("login_timeout");

    if (!str || strlen(str) < MIN_PASSWORD_LEN) {
        write("\nToo short. Please choose a password of at least " +
            MIN_PASSWORD_LEN + " letters: ");
        call_out("login_timeout", LOGIN_TIMEOUT_SECS);
        input_to("got_new_password", INPUT_NOECHO);
        return;
    }

    pending_password = str;
    write("\nPlease confirm your password choice: ");
    call_out("login_timeout", LOGIN_TIMEOUT_SECS);
    input_to("got_confirm_password", INPUT_NOECHO);
}

void
got_confirm_password(string str)
{
    remove_call_out("login_timeout");

    if (str != pending_password) {
        pending_password = 0;
        write("\nPasswords did not match. Please choose a password of at least " +
            MIN_PASSWORD_LEN + " letters: ");
        call_out("login_timeout", LOGIN_TIMEOUT_SECS);
        input_to("got_new_password", INPUT_NOECHO);
        return;
    }

    if (!ACCOUNT_D->create_account(account_name, pending_password)) {
        // Only real failure path left once got_account_name() has
        // already confirmed !account_exists(): create_account()'s own
        // empty-name/empty-password guards, already ruled out above by
        // valid_name()/the MIN_PASSWORD_LEN check.
        write("\nCould not create that account. Goodbye.\n");
        destruct(this_object());
        return;
    }

    pending_password = 0;
    write("\nAccount created.\n");
    // notes/ACCOUNT_LOGIN_PLAN.md build ordering item 4: name the first
    // real character for this brand-new account, rather than reusing the
    // account name unasked the way items 2/3 did.
    write("What would you like to name your character? ");
    call_out("login_timeout", LOGIN_TIMEOUT_SECS);
    input_to("got_character_name");
}

void
got_character_name(string str)
{
    remove_call_out("login_timeout");

    if (!valid_name(str)) {
        write("\nThat is not a valid character name. What would you like to name your character? ");
        call_out("login_timeout", LOGIN_TIMEOUT_SECS);
        input_to("got_character_name");
        return;
    }

    if (!ACCOUNT_D->character_name_available(str)) {
        write("\nThat character name is already taken. What would you like to name your character? ");
        call_out("login_timeout", LOGIN_TIMEOUT_SECS);
        input_to("got_character_name");
        return;
    }

    ACCOUNT_D->add_character(account_name, str);
    character_name = str;
    enter_game();
}
