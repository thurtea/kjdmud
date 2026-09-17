// file: /single/account_d.c
//
// notes/ACCOUNT_LOGIN_PLAN.md's own first build slice, "Account
// storage" (2026-08-21): account_exists(), create_account(),
// check_password(), account_exists(), no login integration yet,
// testable in isolation via eval, matching that plan's own "Rough
// build ordering" item 1 exactly. Every efun this needs (crypt,
// save_object, restore_object, mkdir, file_size) was already confirmed
// real and working before this was built, per that plan's own "Driver
// hooks actually available" section: this is genuinely new mudlib
// code, not a driver gap.
//
// Per-account state lives in a separate /single/account_record.c
// instance, one save_object()/restore_object() file per account under
// ACCOUNTS_DIR, bucketed by the account name's own first letter (see
// that file's own header comment for why a per-account object exists
// at all rather than this daemon just calling save_object() on
// itself).

#include <globals.h>

private
string account_path(string name) {
    name = lower_case(name);
    return ACCOUNTS_DIR + "/" + name[0..0] + "/" + name;
}

private
void ensure_dirs(string name) {
    // mkdir() returns 0 when the directory already exists (real
    // file.c's own mkdir() behavior, ported here too): no need to
    // check first, a redundant call is harmless, matching the "does
    // not create missing parent directories either" contract
    // save_object()'s own registration comment documents, callers that
    // need the tree to exist make it themselves, this is that call.
    mkdir(ACCOUNTS_DIR);
    mkdir(ACCOUNTS_DIR + "/" + lower_case(name)[0..0]);
}

// character_path()/ensure_character_dirs(): notes/ACCOUNT_LOGIN_PLAN.md
// build ordering item 3 (2026-08-21). Public, unlike account_path()/
// ensure_dirs() above: those two are only ever called from inside this
// same file, but /clone/user.c and /clone/login.c both need the
// character-file path now (user.c to save/restore itself directly,
// login.c to pass that path to user.c), and this file is the one place
// the bucketing rule should live (globals.h's own CHARACTERS_DIR
// comment). Identical bucketing convention to account_path() above,
// just under CHARACTERS_DIR instead of ACCOUNTS_DIR -- accounts and
// characters are two separate trees, not two variables holding the
// same file, because account_d.c owns auth data (password hash) and
// user.c owns gameplay state, a real, deliberate separation of concerns
// even though this slice's own single-character-per-account shape means
// the two file names happen to match today.
string character_path(string name) {
    name = lower_case(name);
    return CHARACTERS_DIR + "/" + name[0..0] + "/" + name;
}

void ensure_character_dirs(string name) {
    mkdir(CHARACTERS_DIR);
    mkdir(CHARACTERS_DIR + "/" + lower_case(name)[0..0]);
}

// character_name_available()/add_character()/characters(): notes/
// ACCOUNT_LOGIN_PLAN.md build ordering item 4 (character creation,
// 2026-08-21). Before this item, a character's own name was always the
// same string as its account's name (item 2/3's own shape) -- item 4
// lets a brand-new account's player choose a real, distinct character
// name instead, the first genuine use of account_record.c's own
// `characters` array (built in item 1, never populated until now).
// Because a character's own name (not the account's) now decides its
// bucket under CHARACTERS_DIR, and two different accounts could
// otherwise pick the identical character name, character_name_available()
// exists as its own real availability check, mirroring account_exists()
// above but against the character tree, not the account tree.

int
character_name_available(string char_name) {
    if (!char_name || char_name == "") {
        return 0;
    }
    return file_size(character_path(char_name) + ".o") == -1;
}

// add_character(): records a newly created character's own name against
// its account, called once, right after login.c's own new-character
// naming step confirms the name is both valid and available. Loads the
// account's own record, appends, re-saves -- the same load/mutate/save
// shape check_password() already uses for read-only access, just with a
// write at the end.
//
// Also reserves the character's own file immediately, a real gap caught
// live while testing this item: /clone/user.c's own save_character()
// (net_dead()/remove()) is the only place a character's own file
// actually gets written, and that only fires on the character's first
// real disconnect -- between add_character() here and that first
// disconnect, character_name_available() above would keep reporting
// the just-chosen name as available (no file yet), letting a second,
// unrelated account claim the identical name before the first
// character had ever saved anything. save_object(), called here with
// account_d.c's own current_object() (this file declares no object
// variables at all), writes a real but genuinely empty save file --
// this file existing is the actual reservation; user.c's own later
// load_character()/restore_object() on it just opens successfully,
// reads zero lines, and leaves every variable (login_count included)
// at its already-initialized default, indistinguishable from this
// truly being the character's first login (which it is).
void
add_character(string account_name, string char_name) {
    object rec;
    string path;
    string *chars;

    if (!account_exists(account_name) || !char_name || char_name == "") {
        return;
    }

    path = account_path(account_name);
    rec = new(ACCOUNT_RECORD);
    rec->load_me(path);
    chars = rec->query_characters();
    if (!chars) {
        chars = ({});
    }
    if (member_array(char_name, chars) == -1) {
        chars += ({ char_name });
    }
    rec->set_characters(chars);
    rec->save_me(path);
    destruct(rec);

    ensure_character_dirs(char_name);
    save_object(character_path(char_name));
}

// characters(): the account's own real character list, read by login.c
// on every existing-account login to know which character to load. An
// account created before this item exists on disk with its own
// characters array still empty (item 1/2/3 never populated it) -- an
// empty array is a real, valid answer here, not an error; login.c's own
// caller decides the pre-item-4 fallback (its own account-name-as-
// character-name shape), not this daemon.
string *
characters(string account_name) {
    object rec;
    string *chars;

    if (!account_exists(account_name)) {
        return ({});
    }

    rec = new(ACCOUNT_RECORD);
    rec->load_me(account_path(account_name));
    chars = rec->query_characters();
    destruct(rec);

    return chars ? chars : ({});
}

int
account_exists(string name) {
    if (!name || name == "") {
        return 0;
    }
    return file_size(account_path(name) + ".o") != -1;
}

int
create_account(string name, string password) {
    object rec;
    string path;

    if (!name || name == "" || !password || password == "") {
        return 0;
    }
    if (account_exists(name)) {
        return 0;
    }

    ensure_dirs(name);
    path = account_path(name);

    rec = new(ACCOUNT_RECORD);
    rec->set_name(lower_case(name));
    // Real "crypt(str2, 0)" idiom (secure/std/login.c's own
    // confirm_password(), see crypt()'s own EfunTable.cpp citation):
    // salt 0 generates a fresh random salt for a brand-new password.
    rec->set_hash(crypt(password, 0));
    rec->set_created(time());
    rec->set_characters(({}));
    rec->save_me(path);
    destruct(rec);

    return 1;
}

int
check_password(string name, string password) {
    object rec;
    string path;
    string hash;
    int ok;

    if (!account_exists(name)) {
        return 0;
    }

    path = account_path(name);
    rec = new(ACCOUNT_RECORD);
    rec->load_me(path);
    hash = rec->query_hash();
    // Real crypt() verification idiom: passing the already-stored hash
    // back in as the "salt" argument re-derives it with the same salt
    // crypt(3) itself embeds in the hash's own leading bytes, so a
    // correct password reproduces the identical hash string; confirmed
    // directly from this driver's own crypt() implementation (a
    // string salt of length >= 2 is used as-is, not regenerated).
    ok = hash && crypt(password, hash) == hash;
    destruct(rec);

    return ok;
}
