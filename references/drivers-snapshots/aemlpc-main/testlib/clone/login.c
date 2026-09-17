#include <globals.h>

private string account_name;
private string character_name;
private string pending_password;
private int tries;

void login_timeout() {
    write("\nTimed out waiting for input. Goodbye.\n");
    destruct(this_object());
}

int valid_name(string name) {
    return name && name != "" && strsrch(name, "/") == -1;
}

void enter_game() {
    object user = new(USER_OB);
    user->set_name(character_name);
    user->load_character(ACCOUNT_D->character_path(character_name));
    write("Welcome back. Login count: " + user->query_login_count() + "\n");
    exec(user, this_object());
    user->setup();
    user->move(START_LOC);
    destruct(this_object());
}

void logon() {
    cat("/etc/motd");
    write("\nWhat account name do you wish? ");
    tries = 0;
    call_out("login_timeout", LOGIN_TIMEOUT_SECS);
    input_to("got_account_name");
}

void got_account_name(string str) {
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
        write("\nNo such account. Creating a new account named '" + account_name + "'.\n");
        write("Please choose a password of at least " + MIN_PASSWORD_LEN + " letters: ");
        input_to("got_new_password", INPUT_NOECHO);
    }
}

void got_login_password(string str) {
    string *chars;

    remove_call_out("login_timeout");
    if (ACCOUNT_D->check_password(account_name, str)) {
        chars = ACCOUNT_D->characters(account_name);
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

void got_new_password(string str) {
    remove_call_out("login_timeout");
    if (!str || strlen(str) < MIN_PASSWORD_LEN) {
        write("\nToo short. Please choose a password of at least " + MIN_PASSWORD_LEN + " letters: ");
        call_out("login_timeout", LOGIN_TIMEOUT_SECS);
        input_to("got_new_password", INPUT_NOECHO);
        return;
    }
    pending_password = str;
    write("\nPlease confirm your password choice: ");
    call_out("login_timeout", LOGIN_TIMEOUT_SECS);
    input_to("got_confirm_password", INPUT_NOECHO);
}

void got_confirm_password(string str) {
    remove_call_out("login_timeout");
    if (str != pending_password) {
        pending_password = 0;
        write("\nPasswords did not match. Please choose a password of at least " + MIN_PASSWORD_LEN + " letters: ");
        call_out("login_timeout", LOGIN_TIMEOUT_SECS);
        input_to("got_new_password", INPUT_NOECHO);
        return;
    }
    if (!ACCOUNT_D->create_account(account_name, pending_password)) {
        write("\nCould not create that account. Goodbye.\n");
        destruct(this_object());
        return;
    }
    pending_password = 0;
    write("\nAccount created.\n");
    write("What would you like to name your character? ");
    call_out("login_timeout", LOGIN_TIMEOUT_SECS);
    input_to("got_character_name");
}

void got_character_name(string str) {
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
