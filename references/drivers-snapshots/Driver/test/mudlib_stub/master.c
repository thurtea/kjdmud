// Master object for the minimal test mudlib. This mudlib's only purpose
// is exercising the driver end to end (login, basic movement, a couple
// of commands, object creation) -- not feature parity with any real
// mudlib. See /obj/login.c, /obj/user.c, /obj/item.c, /rooms/start_room.c,
// /rooms/second_room.c for the rest of it.
//
// connect() is invoked by Server::onNewConnection() for every new TCP
// client, via the same applyMaster() mechanism used elsewhere. It must
// return an object -- the driver binds that object to the Connection.
// No account/password persistence, no privs, no simul_efun, no
// compile_object()/virtual-object path -- deliberately out of scope.

object connect() {
    return clone_object("/obj/login");
}
