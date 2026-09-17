#include <globals.h>

string character_path(string name) {
    name = lower_case(name);
    return CHARACTERS_DIR + "/" + name[0..0] + "/" + name;
}

void ensure_character_dirs(string name) {
    mkdir(CHARACTERS_DIR);
    mkdir(CHARACTERS_DIR + "/" + lower_case(name)[0..0]);
}

private string account_path(string name) {
    name = lower_case(name);
    return ACCOUNTS_DIR + "/" + name[0..0] + "/" + name;
}

private void ensure_dirs(string name) {
    mkdir(ACCOUNTS_DIR);
    mkdir(ACCOUNTS_DIR + "/" + lower_case(name)[0..0]);
}

int account_exists(string name) {
    return name && name != "" && file_size(account_path(name) + ".o") != -1;
}

int create_account(string name, string password) {
    object rec;
    string path;

    if (!name || name == "" || !password || password == "" || account_exists(name)) {
        return 0;
    }
    ensure_dirs(name);
    path = account_path(name);
    rec = new(ACCOUNT_RECORD);
    rec->set_name(lower_case(name));
    rec->set_hash(crypt(password, 0));
    rec->set_created(time());
    rec->set_characters(({}));
    rec->save_me(path);
    destruct(rec);
    return 1;
}

int check_password(string name, string password) {
    object rec;
    string path;
    string hash;

    if (!account_exists(name)) {
        return 0;
    }
    path = account_path(name);
    rec = new(ACCOUNT_RECORD);
    rec->load_me(path);
    hash = rec->query_hash();
    destruct(rec);
    return hash && crypt(password, hash) == hash;
}

void add_character(string account_name, string char_name) {
    object rec;
    string path;
    string *chars;

    if (!account_exists(account_name) || !char_name || char_name == "") {
        return;
    }
    path = account_path(account_name);
    rec = new(ACCOUNT_RECORD);
    rec->load_me(path);
    chars = rec->query_characters() || ({});
    if (member_array(char_name, chars) == -1) {
        chars += ({ char_name });
    }
    rec->set_characters(chars);
    rec->save_me(path);
    destruct(rec);

    ensure_character_dirs(char_name);
    save_object(character_path(char_name));
}

string *characters(string account_name) {
    object rec;
    string *chars;

    if (!account_exists(account_name)) {
        return ({});
    }
    rec = new(ACCOUNT_RECORD);
    rec->load_me(account_path(account_name));
    chars = rec->query_characters();
    destruct(rec);
    return chars || ({});
}
