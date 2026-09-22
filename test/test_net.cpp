#include "kjdmud/config/Config.hpp"
#include "kjdmud/efun/EfunTable.hpp"
#include "kjdmud/net/Connection.hpp"
#include "kjdmud/net/InteractiveRegistry.hpp"
#include "kjdmud/net/OutputContext.hpp"
#include "kjdmud/net/Server.hpp"
#include "kjdmud/object/ObjectManager.hpp"
#include "kjdmud/vm/VM.hpp"
#include "kjdmud/vm/Value.hpp"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <openssl/ssl.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

void setNonBlocking(int fd) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

std::string readAvailable(int fd) {
    setNonBlocking(fd);
    std::string out;
    char buf[512];
    for (;;) {
        ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        out.append(buf, static_cast<size_t>(n));
    }
    return out;
}

struct NetHarness {
    std::string tempDir;
    kjdmud::Config config;
    kjdmud::ObjectManager objects;
    kjdmud::VM vm;

    explicit NetHarness(const std::string& extraConfigLines = "")
        : objects(config), vm(objects, config) {
        objects.setVM(&vm);
        char dirTemplate[] = "/tmp/kjdmud_net_test_XXXXXX";
        char* created = mkdtemp(dirTemplate);
        assert(created != nullptr);
        tempDir = created;
        std::string cfgPath = tempDir + "/driver.cfg";
        std::ofstream cfg(cfgPath);
        cfg << "mudlib_root: " << tempDir << "\n";
        cfg << "master_file: /unused\n";
        cfg << "include_dir: " << tempDir << "\n";
        cfg << "port: 0\n";
        cfg << extraConfigLines;
        cfg.close();
        assert(config.loadFromFile(cfgPath));
    }

    void writeFile(const std::string& relPath, const std::string& contents) {
        std::ofstream f(tempDir + relPath);
        f << contents;
    }
};

void testListenConfigSynthesizesTelnetFromPort() {
    NetHarness harness;
    const auto& ports = harness.config.listenPorts();
    assert(ports.size() == 1);
    assert(ports[0].kind == kjdmud::ListenKind::Telnet);
    assert(ports[0].port == 0);
    assert(!ports[0].tls);
    assert(ports[0].slot == 1);
    std::cout << "testListenConfigSynthesizesTelnetFromPort OK\n";
}

void testListenConfigParsesMultipleKindsAndTls() {
    NetHarness harness("listen: telnet 2222\nlisten: websocket 8080\nlisten: telnet 8443 tls\n");
    const auto& ports = harness.config.listenPorts();
    assert(ports.size() == 3);
    assert(ports[0].kind == kjdmud::ListenKind::Telnet && ports[0].port == 2222 && !ports[0].tls);
    assert(ports[1].kind == kjdmud::ListenKind::WebSocket && ports[1].port == 8080 && !ports[1].tls);
    assert(ports[2].kind == kjdmud::ListenKind::Telnet && ports[2].port == 8443 && ports[2].tls);
    std::cout << "testListenConfigParsesMultipleKindsAndTls OK\n";
}

void testSysNetworkPortsReadsTheListenTable() {
    NetHarness harness("listen: telnet 2222\nlisten: websocket 8080 tls\n");
    harness.writeFile("/ports.c", "mixed *probe() { return sys_network_ports(); }\n");
    auto ob = harness.objects.cloneObject("/ports");
    assert(ob);
    kjdmud::Value result = harness.vm.callFunction(ob, "probe", {});
    auto outer = std::get<std::shared_ptr<kjdmud::Array>>(result.data);
    assert(outer && outer->items.size() == 2);
    auto a = std::get<std::shared_ptr<kjdmud::Array>>(outer->items[0].data);
    auto b = std::get<std::shared_ptr<kjdmud::Array>>(outer->items[1].data);
    assert(std::get<std::string>(a->items[1].data) == "telnet");
    assert(std::get<int64_t>(a->items[2].data) == 2222);
    assert(std::get<int64_t>(a->items[3].data) == 0);
    assert(std::get<std::string>(b->items[1].data) == "websocket");
    assert(std::get<int64_t>(b->items[2].data) == 8080);
    assert(std::get<int64_t>(b->items[3].data) == 1);
    std::cout << "testSysNetworkPortsReadsTheListenTable OK\n";
}

void testSaveObjectWritesFluffosTextAndSecondProcessRestores() {
    NetHarness writer;
    writer.writeFile("/save_probe.c",
        "int n;\n"
        "string s;\n"
        "void create() { n = 42; s = \"hello\"; }\n"
        "int save() { return save_object(\"/probe.o\"); }\n"
        "int load() { return restore_object(\"/probe.o\"); }\n"
        "int query_n() { return n; }\n"
        "string query_s() { return s; }\n");
    auto obj = writer.objects.cloneObject("/save_probe");
    assert(obj);
    kjdmud::Value saved = writer.vm.callFunction(obj, "save", {});
    assert(std::get<int64_t>(saved.data) == 1);

    std::string diskPath = writer.tempDir + "/probe.o";
    std::ifstream in(diskPath);
    std::ostringstream raw;
    raw << in.rdbuf();
    std::string text = raw.str();
    assert(text.find('#') == 0);
    assert(text.find('\t') == std::string::npos);
    assert(text.find("n 42") != std::string::npos);
    assert(text.find("s \"hello\"") != std::string::npos);

    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        std::ifstream childIn(diskPath);
        std::ostringstream childRaw;
        childRaw << childIn.rdbuf();
        std::string childText = childRaw.str();
        bool ok = !childText.empty() && childText[0] == '#' &&
                  childText.find("n 42") != std::string::npos;
        _exit(ok ? 0 : 1);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);

    NetHarness reader;
    reader.writeFile("/save_probe.c",
        "int n;\n"
        "string s;\n"
        "void create() { n = 0; s = \"\"; }\n"
        "int load() { return restore_object(\"/probe.o\"); }\n"
        "int query_n() { return n; }\n"
        "string query_s() { return s; }\n");
    {
        std::ifstream src(diskPath, std::ios::binary);
        std::ofstream dst(reader.tempDir + "/probe.o", std::ios::binary);
        dst << src.rdbuf();
    }
    auto other = reader.objects.cloneObject("/save_probe");
    assert(other);
    assert(std::get<int64_t>(reader.vm.callFunction(other, "load", {}).data) == 1);
    assert(std::get<int64_t>(reader.vm.callFunction(other, "query_n", {}).data) == 42);
    assert(std::get<std::string>(reader.vm.callFunction(other, "query_s", {}).data) == "hello");
    std::cout << "testSaveObjectWritesFluffosTextAndSecondProcessRestores OK\n";
}

void testSaveObjectWidthGreaterThanOneWritesAndRestoresExtraColumns() {
    NetHarness harness("dialect: ldmud\n");
    harness.writeFile("/save_wide.c",
        "mapping m;\n"
        "void create() { m = ([\"weakness\": \"fire\"; 1]); }\n"
        "void clear() { m = 0; }\n"
        "int save() { return save_object(\"/wide.o\"); }\n"
        "int load() { return restore_object(\"/wide.o\"); }\n"
        "mixed col0() { return m[\"weakness\"]; }\n"
        "mixed col1() { return m[\"weakness\", 1]; }\n");
    auto obj = harness.objects.cloneObject("/save_wide");
    assert(obj);
    assert(std::get<int64_t>(harness.vm.callFunction(obj, "save", {}).data) == 1);

    std::ifstream in(harness.tempDir + "/wide.o");
    std::ostringstream raw;
    raw << in.rdbuf();
    std::string text = raw.str();
    assert(text.find("weakness") != std::string::npos);
    assert(text.find(';') != std::string::npos);
    assert(text.find("fire") != std::string::npos);

    harness.vm.callFunction(obj, "clear", {});
    assert(std::get<int64_t>(harness.vm.callFunction(obj, "load", {}).data) == 1);
    assert(std::get<std::string>(harness.vm.callFunction(obj, "col0", {}).data) == "fire");
    assert(std::get<int64_t>(harness.vm.callFunction(obj, "col1", {}).data) == 1);
    std::cout << "testSaveObjectWidthGreaterThanOneWritesAndRestoresExtraColumns OK\n";
}

void testSaveObjectObjectAndClosureSlotsWriteEmptyAndRestoreZero() {
    NetHarness harness;
    harness.writeFile("/save_unsavable.c",
        "object ob;\n"
        "function fn;\n"
        "void create() { ob = this_object(); fn = (: create :); }\n"
        "void clear() { ob = 1; fn = 1; }\n"
        "int save() { return save_object(\"/unsavable.o\"); }\n"
        "int load() { return restore_object(\"/unsavable.o\"); }\n"
        "int ob_zero() { return ob == 0; }\n"
        "int fn_zero() { return fn == 0; }\n");
    auto obj = harness.objects.cloneObject("/save_unsavable");
    assert(obj);
    assert(std::get<int64_t>(harness.vm.callFunction(obj, "save", {}).data) == 1);

    std::ifstream in(harness.tempDir + "/unsavable.o");
    std::string line;
    bool sawOb = false;
    bool sawFn = false;
    while (std::getline(in, line)) {
        if (line.rfind("ob ", 0) == 0) {
            sawOb = true;
            assert(line == "ob " || line == "ob");
        }
        if (line.rfind("fn ", 0) == 0) {
            sawFn = true;
            assert(line == "fn " || line == "fn");
        }
    }
    assert(sawOb && sawFn);

    harness.vm.callFunction(obj, "clear", {});
    assert(std::get<int64_t>(harness.vm.callFunction(obj, "load", {}).data) == 1);
    assert(std::get<int64_t>(harness.vm.callFunction(obj, "ob_zero", {}).data) == 1);
    assert(std::get<int64_t>(harness.vm.callFunction(obj, "fn_zero", {}).data) == 1);
    std::cout << "testSaveObjectObjectAndClosureSlotsWriteEmptyAndRestoreZero OK\n";
}

void testWebSocketHandshakeAndTextFrame() {
    int fds[2];
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    setNonBlocking(fds[0]);
    kjdmud::Connection conn(fds[0]);
    conn.enableWebSocket();
    conn.send("BANNER\n");
    assert(readAvailable(fds[1]).empty());

    std::string req =
        "GET / HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "\r\n";
    assert(::write(fds[1], req.data(), req.size()) == static_cast<ssize_t>(req.size()));
    auto lines = conn.pollLines();
    assert(lines.empty());
    assert(conn.transportReady());

    std::string resp = readAvailable(fds[1]);
    assert(resp.find("HTTP/1.1 101") == 0);
    assert(resp.find("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != std::string::npos);
    assert(resp.find("BANNER") != std::string::npos);

    // Masked client text frame for "hi\n" (pollLines splits on newline).
    unsigned char mask[4] = {0x37, 0xfa, 0x21, 0x3d};
    const char payload[] = {'h', 'i', '\n'};
    std::string frame;
    frame.push_back(static_cast<char>(0x81));
    frame.push_back(static_cast<char>(0x80 | 3));
    frame.append(reinterpret_cast<char*>(mask), 4);
    for (int i = 0; i < 3; ++i) {
        frame.push_back(static_cast<char>(payload[i] ^ mask[i]));
    }
    assert(::write(fds[1], frame.data(), frame.size()) == static_cast<ssize_t>(frame.size()));
    lines = conn.pollLines();
    assert(lines.size() == 1);
    assert(lines[0] == "hi");

    conn.send("yo\n");
    std::string wired = readAvailable(fds[1]);
    assert(!wired.empty());
    assert(static_cast<unsigned char>(wired[0]) == 0x81);

    ::close(fds[1]);
    std::cout << "testWebSocketHandshakeAndTextFrame OK\n";
}

bool writeSelfSignedCert(const std::string& dir, std::string& cert, std::string& key) {
    cert = dir + "/cert.pem";
    key = dir + "/key.pem";
    std::string cmd =
        "PATH=\"/opt/homebrew/opt/openssl@3/bin:/opt/homebrew/bin:$PATH\" "
        "openssl req -x509 -newkey rsa:2048 -keyout " + key +
        " -out " + cert +
        " -days 1 -nodes -subj /CN=localhost >/dev/null 2>&1";
    return std::system(cmd.c_str()) == 0;
}

void testTlsSocketpairRoundTrip() {
    char dirTemplate[] = "/tmp/kjdmud_tls_XXXXXX";
    char* created = mkdtemp(dirTemplate);
    assert(created);
    std::string cert, key;
    if (!writeSelfSignedCert(created, cert, key)) {
        std::cout << "testTlsSocketpairRoundTrip SKIPPED (openssl req failed)\n";
        return;
    }

    OPENSSL_init_ssl(0, nullptr);
    SSL_CTX* serverCtx = SSL_CTX_new(TLS_server_method());
    SSL_CTX* clientCtx = SSL_CTX_new(TLS_client_method());
    assert(serverCtx && clientCtx);
    SSL_CTX_set_verify(clientCtx, SSL_VERIFY_NONE, nullptr);
    assert(SSL_CTX_use_certificate_file(serverCtx, cert.c_str(), SSL_FILETYPE_PEM) == 1);
    assert(SSL_CTX_use_PrivateKey_file(serverCtx, key.c_str(), SSL_FILETYPE_PEM) == 1);

    int fds[2];
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);

    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        ::close(fds[0]);
        SSL* ssl = SSL_new(clientCtx);
        SSL_set_fd(ssl, fds[1]);
        int rc = SSL_connect(ssl);
        if (rc != 1) _exit(2);
        SSL_write(ssl, "hello\n", 6);
        char buf[32] = {};
        int n = SSL_read(ssl, buf, sizeof(buf));
        bool ok = n > 0 && std::string(buf, static_cast<size_t>(n)).find("ok") != std::string::npos;
        SSL_free(ssl);
        _exit(ok ? 0 : 3);
    }

    ::close(fds[1]);
    kjdmud::Connection conn(fds[0]);
    assert(conn.acceptTls(serverCtx));
    assert(conn.isTls());
    setNonBlocking(fds[0]);
    auto lines = conn.pollLines();
    if (lines.empty()) {
        for (int i = 0; i < 20 && lines.empty(); ++i) {
            usleep(10000);
            lines = conn.pollLines();
        }
    }
    assert(!lines.empty());
    assert(lines[0] == "hello");
    conn.send("ok\n");

    int status = 0;
    waitpid(pid, &status, 0);
    SSL_CTX_free(serverCtx);
    SSL_CTX_free(clientCtx);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    std::cout << "testTlsSocketpairRoundTrip OK\n";
}

void testEncodingAndGmcpEfunsOnASocketpair() {
    NetHarness harness;
    harness.writeFile("/enc.c",
        "int set_it(string e) { return set_encoding(e); }\n"
        "string get_it() { return query_encoding(); }\n"
        "int tls() { return query_connection_tls(); }\n"
        "int gmcp() { return has_gmcp(); }\n"
        "void send_it() { send_gmcp(\"Core.Ping\"); }\n");
    auto obj = harness.objects.cloneObject("/enc");
    assert(obj);

    int fds[2];
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    kjdmud::Connection conn(fds[0]);
    conn.attach(obj);
    harness.vm.pushCommandGiver(obj);

    kjdmud::Value enc = harness.vm.callFunction(obj, "get_it", {});
    assert(std::get<std::string>(enc.data) == "utf-8");
    assert(std::get<int64_t>(harness.vm.callFunction(obj, "set_it", {kjdmud::Value(std::string("iso-8859-1"))}).data) == 1);
    enc = harness.vm.callFunction(obj, "get_it", {});
    assert(std::get<std::string>(enc.data) == "iso-8859-1");
    assert(std::get<int64_t>(harness.vm.callFunction(obj, "tls", {}).data) == 0);
    assert(std::get<int64_t>(harness.vm.callFunction(obj, "gmcp", {}).data) == 0);

    conn.setGmcpEnabled(true);
    assert(std::get<int64_t>(harness.vm.callFunction(obj, "gmcp", {}).data) == 1);
    harness.vm.callFunction(obj, "send_it", {});
    std::string wired = readAvailable(fds[1]);
    assert(wired.find("Core.Ping") != std::string::npos);
    assert(static_cast<unsigned char>(wired[0]) == 255);
    assert(static_cast<unsigned char>(wired[2]) == 201);

    harness.vm.popCommandGiver();
    ::close(fds[1]);
    std::cout << "testEncodingAndGmcpEfunsOnASocketpair OK\n";
}

void testGmcpSubnegotiationIsQueued() {
    int fds[2];
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    setNonBlocking(fds[0]);
    kjdmud::Connection conn(fds[0]);
    unsigned char sb[] = {255, 250, 201, 'C', 'o', 'r', 'e', '.', 'H', 'e', 'l', 'l', 'o', 255, 240, '\n'};
    assert(::write(fds[1], sb, sizeof(sb)) == static_cast<ssize_t>(sizeof(sb)));
    auto lines = conn.pollLines();
    assert(lines.size() == 1);
    assert(lines[0].empty());
    auto gmcp = conn.takeIncomingGmcp();
    assert(gmcp.size() == 1);
    assert(gmcp[0] == "Core.Hello");
    assert(conn.gmcpEnabled());
    ::close(fds[1]);
    std::cout << "testGmcpSubnegotiationIsQueued OK\n";
}

void testEncodingAndMsdpEfunsOnASocketpair() {
    NetHarness harness;
    harness.writeFile("/msdp.c",
        "int msdp() { return has_msdp(); }\n"
        "void send_it() { send_msdp(\"ROOM_NAME\", \"Gatehouse\"); }\n");
    auto obj = harness.objects.cloneObject("/msdp");
    assert(obj);

    int fds[2];
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    kjdmud::Connection conn(fds[0]);
    conn.attach(obj);
    harness.vm.pushCommandGiver(obj);

    assert(std::get<int64_t>(harness.vm.callFunction(obj, "msdp", {}).data) == 0);
    conn.setMsdpEnabled(true);
    assert(std::get<int64_t>(harness.vm.callFunction(obj, "msdp", {}).data) == 1);

    harness.vm.callFunction(obj, "send_it", {});
    std::string wired = readAvailable(fds[1]);
    assert(static_cast<unsigned char>(wired[0]) == 255);
    assert(static_cast<unsigned char>(wired[1]) == 250);
    assert(static_cast<unsigned char>(wired[2]) == 69);
    assert(static_cast<unsigned char>(wired[3]) == 1);
    assert(wired.find("ROOM_NAME") != std::string::npos);
    assert(wired.find("Gatehouse") != std::string::npos);
    assert(static_cast<unsigned char>(wired[wired.size() - 2]) == 255);
    assert(static_cast<unsigned char>(wired[wired.size() - 1]) == 240);

    harness.vm.popCommandGiver();
    ::close(fds[1]);
    std::cout << "testEncodingAndMsdpEfunsOnASocketpair OK\n";
}

// A client volunteering "IAC WILL MSDP" unprompted (no Server::
// onNewConnection() proactive offer run first in this raw-socketpair
// harness), same shape as testGmcpSubnegotiationIsQueued's own missing
// negotiation. handleNegotiation()'s Will-branch reply-and-enable path
// is what this covers; handleSubnegotiation() then queues the payload.
void testMsdpSubnegotiationIsQueued() {
    int fds[2];
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    setNonBlocking(fds[0]);
    kjdmud::Connection conn(fds[0]);
    unsigned char sb[] = {
        255, 250, 69, 1, 'R', 'O', 'O', 'M', '_', 'N', 'A', 'M', 'E',
        2, 'G', 'a', 't', 'e', 'h', 'o', 'u', 's', 'e', 255, 240, '\n'};
    assert(::write(fds[1], sb, sizeof(sb)) == static_cast<ssize_t>(sizeof(sb)));
    auto lines = conn.pollLines();
    assert(lines.size() == 1);
    assert(lines[0].empty());
    auto msdp = conn.takeIncomingMsdp();
    assert(msdp.size() == 1);
    assert(msdp[0].first == "ROOM_NAME");
    assert(msdp[0].second == "Gatehouse");
    assert(conn.msdpEnabled());
    ::close(fds[1]);
    std::cout << "testMsdpSubnegotiationIsQueued OK\n";
}

// The client's "IAC DO MSDP" reply to this driver's own proactive
// "IAC WILL MSDP" (Server::onNewConnection()), same negotiation shape
// MSSP's own test covers for its Do-branch, minus a fixed data block
// to assert on since MSDP has none (see Connection::sendMsdp()'s own
// comment: it sends arbitrary named variables, not one static block).
// has_msp()/telnet_msp_oob() real signatures (core.spec, confirmed
// against current FluffOS source, see Connection.hpp's own
// mspEnabled() comment): telnet_msp_oob operates on current_object, so
// this mirrors testEncodingAndGmcpEfunsOnASocketpair's shape (current
// command_giver/object, not a passed-in object arg). Also covers the
// real "if (ip->iflags & USING_MSP)" silent-no-op guard: the oob send
// before negotiation must write nothing to the wire at all, not an
// empty subnegotiation.
void testMspEfunsWireBytesAndSilentNoopWhenDisabled() {
    NetHarness harness;
    harness.writeFile("/msp.c",
        "int has() { return has_msp(); }\n"
        "void oob(string s) { telnet_msp_oob(s); }\n");
    auto obj = harness.objects.cloneObject("/msp");
    assert(obj);

    int fds[2];
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    setNonBlocking(fds[0]);
    kjdmud::Connection conn(fds[0]);
    conn.attach(obj);
    harness.vm.pushCommandGiver(obj);

    assert(std::get<int64_t>(harness.vm.callFunction(obj, "has", {}).data) == 0);
    harness.vm.callFunction(obj, "oob", {kjdmud::Value(std::string("!!SOUND(bang.wav)"))});
    assert(readAvailable(fds[1]).empty());

    unsigned char willMsp[] = {255, 251, 90};
    assert(::write(fds[1], willMsp, sizeof(willMsp)) == static_cast<ssize_t>(sizeof(willMsp)));
    conn.pollLines();
    readAvailable(fds[1]);  // drain the driver's own "IAC DO MSP" reply
    assert(std::get<int64_t>(harness.vm.callFunction(obj, "has", {}).data) == 1);

    harness.vm.callFunction(obj, "oob", {kjdmud::Value(std::string("!!SOUND(bang.wav)"))});
    std::string wired = readAvailable(fds[1]);
    assert(static_cast<unsigned char>(wired[0]) == 255);
    assert(static_cast<unsigned char>(wired[1]) == 250);
    assert(static_cast<unsigned char>(wired[2]) == 90);
    assert(wired.find("!!SOUND(bang.wav)") != std::string::npos);
    assert(static_cast<unsigned char>(wired[wired.size() - 2]) == 255);
    assert(static_cast<unsigned char>(wired[wired.size() - 1]) == 240);

    // Real telnet_send() (libtelnet.c, used by telnet_subnegotiation()
    // for the payload portion) doubles any literal IAC (0xFF) byte in
    // the payload before it hits the wire; an unescaped 0xFF would read
    // to a real client as a new telnet command starting mid-payload.
    // sendMspOob() must match, since telnet_msp_oob(string) takes fully
    // arbitrary mudlib-authored content by design.
    harness.vm.callFunction(obj, "oob",
        {kjdmud::Value(std::string("bef\xff""ore", 7))});
    std::string escaped = readAvailable(fds[1]);
    // "bef" + FF FF (escaped) + "ore" = 3 + 2 + 3 = 8 payload bytes,
    // plus the 5-byte IAC SB 90 ... IAC SE frame around it = 13 total.
    assert(escaped.size() == 13);
    assert(static_cast<unsigned char>(escaped[0]) == 255);
    assert(static_cast<unsigned char>(escaped[1]) == 250);
    assert(static_cast<unsigned char>(escaped[2]) == 90);
    assert(escaped.substr(3, 3) == "bef");
    assert(static_cast<unsigned char>(escaped[6]) == 255);
    assert(static_cast<unsigned char>(escaped[7]) == 255);
    assert(escaped.substr(8, 3) == "ore");
    assert(static_cast<unsigned char>(escaped[11]) == 255);
    assert(static_cast<unsigned char>(escaped[12]) == 240);

    harness.vm.popCommandGiver();
    ::close(fds[1]);
    std::cout << "testMspEfunsWireBytesAndSilentNoopWhenDisabled OK\n";
}

// The client's "IAC DO MSP" reply to this driver's own proactive
// "IAC WILL MSP" offer, same shape as testMsdpDoReplySetsEnabledFlag,
// plus the one-shot mspEnableNegotiated_ flag MSDP/MXP have no
// equivalent of (see Connection.hpp's own mspEnabled() comment on why
// MSP alone needs it: real code fires an LPC apply here).
void testMspDoReplySetsEnabledFlagsOnce() {
    int fds[2];
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    setNonBlocking(fds[0]);
    kjdmud::Connection conn(fds[0]);

    unsigned char doMsp[] = {255, 253, 90};
    assert(::write(fds[1], doMsp, sizeof(doMsp)) == static_cast<ssize_t>(sizeof(doMsp)));
    auto lines = conn.pollLines();
    assert(lines.empty());
    assert(conn.mspEnabled());
    assert(conn.takeMspEnableNegotiated());
    assert(!conn.takeMspEnableNegotiated());

    ::close(fds[1]);
    std::cout << "testMspDoReplySetsEnabledFlagsOnce OK\n";
}

// The one piece of MSP behavior no other protocol row in this file
// exercises: a real mudlib apply (msp_enable()) actually firing,
// confirmed against current FluffOS source (src/net/msp.cc's own
// "safe_apply(APPLY_MSP_ENABLE, ip->ob, 0, ORIGIN_DRIVER)"). Drives
// Server::fireMspEnableIfNegotiated() directly (Server.hpp's own
// comment: pulled out static, same testable-seam shape as
// dispatchLine()/fireNetDeadIfLinkDead() above it), not the private
// handleConnection() - no live Server/accept loop needed, just a VM and
// a socketpair-backed Connection.
void testMspEnableFiresMudlibApplyOnce() {
    NetHarness harness;
    harness.writeFile("/msp_obj.c",
        "int enable_calls = 0;\n"
        "void msp_enable() { enable_calls += 1; }\n"
        "int enable_call_count() { return enable_calls; }\n");
    auto obj = harness.objects.cloneObject("/msp_obj");
    assert(obj);

    int fds[2];
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    setNonBlocking(fds[0]);
    kjdmud::Connection conn(fds[0]);
    conn.attach(obj);

    assert(std::get<int64_t>(harness.vm.callFunction(obj, "enable_call_count", {}).data) == 0);

    unsigned char doMsp[] = {255, 253, 90};
    assert(::write(fds[1], doMsp, sizeof(doMsp)) == static_cast<ssize_t>(sizeof(doMsp)));
    conn.pollLines();
    kjdmud::Server::fireMspEnableIfNegotiated(harness.vm, conn);

    assert(std::get<int64_t>(harness.vm.callFunction(obj, "enable_call_count", {}).data) == 1);
    // One-shot: a second call with the flag already consumed must not
    // re-fire. enable_calls increments (not just sets a bool), so this
    // directly proves no double-fire through the real dispatch path,
    // not only through the separate flag-level assertion in
    // testMspDoReplySetsEnabledFlagsOnce.
    kjdmud::Server::fireMspEnableIfNegotiated(harness.vm, conn);
    assert(std::get<int64_t>(harness.vm.callFunction(obj, "enable_call_count", {}).data) == 1);

    ::close(fds[1]);
    std::cout << "testMspEnableFiresMudlibApplyOnce OK\n";
}

// mxp_bold/mxp_color/mxp_link all take a required target object (see
// NetEfuns.cpp's own connectionForRequiredObjectArg() comment), so this
// mirrors testEncodingAndMsdpEfunsOnASocketpair's shape but passes the
// interactive object itself as an explicit LPC-side argument rather than
// relying on the current command_giver.
void testMxpEfunsWrapTextOnlyWhenEnabled() {
    NetHarness harness;
    harness.writeFile("/mxp.c",
        "int has(object ob) { return has_mxp(ob); }\n"
        "string bold(object ob) { return mxp_bold(ob, \"loud\"); }\n"
        "string color(object ob) { return mxp_color(ob, \"blood\", \"red\", \"black\"); }\n"
        "string link(object ob) { return mxp_link(ob, \"north\", \"go north\"); }\n");
    auto obj = harness.objects.cloneObject("/mxp");
    assert(obj);

    int fds[2];
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    kjdmud::Connection conn(fds[0]);
    conn.attach(obj);

    std::vector<kjdmud::Value> objArg{kjdmud::Value(obj)};
    assert(std::get<int64_t>(harness.vm.callFunction(obj, "has", objArg).data) == 0);
    assert(std::get<std::string>(harness.vm.callFunction(obj, "bold", objArg).data) == "loud");
    assert(std::get<std::string>(harness.vm.callFunction(obj, "color", objArg).data) == "blood");
    assert(std::get<std::string>(harness.vm.callFunction(obj, "link", objArg).data) == "north");

    conn.setMxpEnabled(true);
    assert(std::get<int64_t>(harness.vm.callFunction(obj, "has", objArg).data) == 1);
    assert(std::get<std::string>(harness.vm.callFunction(obj, "bold", objArg).data) == "<B>loud</B>");
    assert(std::get<std::string>(harness.vm.callFunction(obj, "color", objArg).data) ==
           "<COLOR FORE=red BACK=black>blood</COLOR>");
    assert(std::get<std::string>(harness.vm.callFunction(obj, "link", objArg).data) ==
           "<SEND \"go north\">north</SEND>");

    ::close(fds[1]);
    std::cout << "testMxpEfunsWrapTextOnlyWhenEnabled OK\n";
}

// A client volunteering "IAC WILL MXP" unprompted, same shape as
// testMsdpSubnegotiationIsQueued's own missing-negotiation setup:
// handleNegotiation()'s Will-branch replies "IAC DO MXP" and enables
// the flag, no subnegotiation payload involved (see Connection.hpp's
// own mxpEnabled() comment on why there is nothing to parse for v1).
void testMxpWillNegotiationRepliesDoAndSetsEnabledFlag() {
    int fds[2];
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    setNonBlocking(fds[0]);
    kjdmud::Connection conn(fds[0]);

    unsigned char willMxp[] = {255, 251, 91};
    assert(::write(fds[1], willMxp, sizeof(willMxp)) == static_cast<ssize_t>(sizeof(willMxp)));
    auto lines = conn.pollLines();
    assert(lines.empty());
    assert(conn.mxpEnabled());

    std::string wired = readAvailable(fds[1]);
    unsigned char expected[] = {255, 253, 91};
    assert(wired == std::string(reinterpret_cast<char*>(expected), sizeof(expected)));

    ::close(fds[1]);
    std::cout << "testMxpWillNegotiationRepliesDoAndSetsEnabledFlag OK\n";
}

// The client's "IAC DO MXP" reply to this driver's own proactive
// "IAC WILL MXP" (Server::onNewConnection()), same shape as
// testMsdpDoReplySetsEnabledFlag just below.
void testMxpDoReplySetsEnabledFlag() {
    int fds[2];
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    setNonBlocking(fds[0]);
    kjdmud::Connection conn(fds[0]);

    unsigned char doMxp[] = {255, 253, 91};
    assert(::write(fds[1], doMxp, sizeof(doMxp)) == static_cast<ssize_t>(sizeof(doMxp)));
    auto lines = conn.pollLines();
    assert(lines.empty());
    assert(conn.mxpEnabled());

    ::close(fds[1]);
    std::cout << "testMxpDoReplySetsEnabledFlag OK\n";
}

void testMsdpDoReplySetsEnabledFlag() {
    int fds[2];
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    setNonBlocking(fds[0]);
    kjdmud::Connection conn(fds[0]);

    unsigned char doMsdp[] = {255, 253, 69};
    assert(::write(fds[1], doMsdp, sizeof(doMsdp)) == static_cast<ssize_t>(sizeof(doMsdp)));
    auto lines = conn.pollLines();
    assert(lines.empty());
    assert(conn.msdpEnabled());

    ::close(fds[1]);
    std::cout << "testMsdpDoReplySetsEnabledFlag OK\n";
}

// A client volunteering "IAC WILL ZMP" unprompted, same shape as
// testMxpWillNegotiationRepliesDoAndSetsEnabledFlag: verifies both the
// reply bytes and the resulting flag.
void testZmpWillNegotiationRepliesDoAndSetsEnabledFlag() {
    int fds[2];
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    setNonBlocking(fds[0]);
    kjdmud::Connection conn(fds[0]);

    unsigned char willZmp[] = {255, 251, 93};
    assert(::write(fds[1], willZmp, sizeof(willZmp)) == static_cast<ssize_t>(sizeof(willZmp)));
    auto lines = conn.pollLines();
    assert(lines.empty());
    assert(conn.zmpEnabled());

    std::string wired = readAvailable(fds[1]);
    unsigned char expected[] = {255, 253, 93};
    assert(wired == std::string(reinterpret_cast<char*>(expected), sizeof(expected)));

    ::close(fds[1]);
    std::cout << "testZmpWillNegotiationRepliesDoAndSetsEnabledFlag OK\n";
}

// The client's "IAC DO ZMP" reply to this driver's own proactive
// "IAC WILL ZMP" offer, same shape as testMsdpDoReplySetsEnabledFlag
// just above. Unlike MSP, no one-shot apply-fire flag exists for ZMP
// (see Connection.hpp's own zmpEnabled() comment on why: real
// on_telnet_do()'s own ZMP case fires no apply on negotiation).
void testZmpDoReplySetsEnabledFlag() {
    int fds[2];
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    setNonBlocking(fds[0]);
    kjdmud::Connection conn(fds[0]);

    unsigned char doZmp[] = {255, 253, 93};
    assert(::write(fds[1], doZmp, sizeof(doZmp)) == static_cast<ssize_t>(sizeof(doZmp)));
    auto lines = conn.pollLines();
    assert(lines.empty());
    assert(conn.zmpEnabled());

    ::close(fds[1]);
    std::cout << "testZmpDoReplySetsEnabledFlag OK\n";
}

// Direct Connection::sendZmp() wire-format test: real framing is
// "IAC SB ZMP <command>\0<arg1>\0<arg2>\0 IAC SE" (verified against
// libtelnet.c's telnet_send_zmp()/telnet_zmp_arg()/_zmp_telnet(), see
// Connection.hpp's own sendZmp() comment), with any literal IAC byte in
// a field doubled first. Also covers the "unsupported/unnegotiated
// connection" case directly: real f_send_zmp() has no USING_ZMP guard
// at all (see sendZmp()'s own .cpp comment, verified against current
// FluffOS source, not assumed from MSP's own different behavior), so
// this asserts the send still produces wire bytes with ZMP never
// negotiated - the correct verified behavior, not a missed guard.
void testZmpSendFramingEscapesIacAndWorksEvenWithoutNegotiation() {
    int fds[2];
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    setNonBlocking(fds[0]);
    kjdmud::Connection conn(fds[0]);

    assert(!conn.zmpEnabled());
    conn.sendZmp("zmp.ping", {std::string("bef\xff""ore", 7), std::string("plain")});
    std::string wired = readAvailable(fds[1]);

    // "IAC SB 93" (3) + "zmp.ping\0" (9) + "bef" FF FF "ore\0" (9) +
    // "plain\0" (6) + "IAC SE" (2) = 29 bytes.
    assert(wired.size() == 29);
    assert(static_cast<unsigned char>(wired[0]) == 255);
    assert(static_cast<unsigned char>(wired[1]) == 250);
    assert(static_cast<unsigned char>(wired[2]) == 93);
    assert(wired.substr(3, 9) == std::string("zmp.ping\0", 9));
    assert(wired.substr(12, 9) == std::string("bef\xff\xff""ore\0", 9));
    assert(wired.substr(21, 6) == std::string("plain\0", 6));
    assert(static_cast<unsigned char>(wired[wired.size() - 2]) == 255);
    assert(static_cast<unsigned char>(wired[wired.size() - 1]) == 240);

    ::close(fds[1]);
    std::cout << "testZmpSendFramingEscapesIacAndWorksEvenWithoutNegotiation OK\n";
}

// Inbound parsing: "IAC SB ZMP cmd\0arg1\0arg2\0 IAC SE" must split into
// a command and an args vector, and set zmpEnabled_ as a side effect
// the same way GMCP's/MSDP's own subnegotiation branches already do.
void testZmpInboundParsingQueuesCommandAndArgs() {
    int fds[2];
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    setNonBlocking(fds[0]);
    kjdmud::Connection conn(fds[0]);

    std::vector<unsigned char> sb = {255, 250, 93};
    for (char c : std::string("zmp.ping\0arg1\0arg2\0", 19)) {
        sb.push_back(static_cast<unsigned char>(c));
    }
    sb.push_back(255);
    sb.push_back(240);
    sb.push_back('\n');
    assert(::write(fds[1], sb.data(), sb.size()) == static_cast<ssize_t>(sb.size()));

    auto lines = conn.pollLines();
    assert(lines.size() == 1);
    assert(lines[0].empty());
    assert(conn.zmpEnabled());

    auto zmp = conn.takeIncomingZmp();
    assert(zmp.size() == 1);
    assert(zmp[0].first == "zmp.ping");
    assert(zmp[0].second.size() == 2);
    assert(zmp[0].second[0] == "arg1");
    assert(zmp[0].second[1] == "arg2");
    assert(conn.takeIncomingZmp().empty());

    ::close(fds[1]);
    std::cout << "testZmpInboundParsingQueuesCommandAndArgs OK\n";
}

// A ZMP command with no arguments at all ("cmd\0" only) must still
// queue correctly with an empty args vector, not crash or drop the
// command - the fields.begin()+1 == fields.end() edge case in
// handleSubnegotiation()'s own ZMP branch.
void testZmpInboundParsingHandlesCommandWithNoArgs() {
    int fds[2];
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    setNonBlocking(fds[0]);
    kjdmud::Connection conn(fds[0]);

    std::vector<unsigned char> sb = {255, 250, 93};
    for (char c : std::string("zmp.ping\0", 9)) {
        sb.push_back(static_cast<unsigned char>(c));
    }
    sb.push_back(255);
    sb.push_back(240);
    assert(::write(fds[1], sb.data(), sb.size()) == static_cast<ssize_t>(sb.size()));
    conn.pollLines();

    auto zmp = conn.takeIncomingZmp();
    assert(zmp.size() == 1);
    assert(zmp[0].first == "zmp.ping");
    assert(zmp[0].second.empty());

    ::close(fds[1]);
    std::cout << "testZmpInboundParsingHandlesCommandWithNoArgs OK\n";
}

// Real _zmp_telnet() (libtelnet.c) rejects a payload that does not end
// in a NUL byte as an incomplete frame - no event fires at all, not a
// partial one. A malformed frame here must not queue anything, though
// the connection is still marked ZMP-capable (matching the sibling
// GMCP/MSDP branches, which set their own enabled flag unconditionally
// on hitting their branch at all, negotiation success already implied
// by reaching this code path).
void testZmpMalformedInboundFrameNotEndingInNulIsDiscarded() {
    int fds[2];
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    setNonBlocking(fds[0]);
    kjdmud::Connection conn(fds[0]);

    // "cmd\0arg" - no trailing NUL after "arg".
    std::vector<unsigned char> sb = {255, 250, 93};
    for (char c : std::string("cmd\0arg", 7)) {
        sb.push_back(static_cast<unsigned char>(c));
    }
    sb.push_back(255);
    sb.push_back(240);
    assert(::write(fds[1], sb.data(), sb.size()) == static_cast<ssize_t>(sb.size()));
    conn.pollLines();

    assert(conn.takeIncomingZmp().empty());
    assert(conn.zmpEnabled());

    ::close(fds[1]);
    std::cout << "testZmpMalformedInboundFrameNotEndingInNulIsDiscarded OK\n";
}

// has_zmp()/send_zmp() real signatures (core.spec, confirmed against
// current FluffOS source, see NetEfuns.cpp's own has_zmp registration
// comment): send_zmp operates on command_giver (the opposite of
// telnet_msp_oob's current_object), and silently filters out any
// non-string array element rather than erroring.
void testZmpEfunsUseRealSignaturesAndFilterNonStringArgs() {
    NetHarness harness;
    harness.writeFile("/zmp.c",
        "int has() { return has_zmp(); }\n"
        "void send_it() { send_zmp(\"zmp.ping\", ({ \"a\", 7, \"b\" })); }\n");
    auto obj = harness.objects.cloneObject("/zmp");
    assert(obj);

    int fds[2];
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    setNonBlocking(fds[0]);
    kjdmud::Connection conn(fds[0]);
    conn.attach(obj);
    harness.vm.pushCommandGiver(obj);

    assert(std::get<int64_t>(harness.vm.callFunction(obj, "has", {}).data) == 0);

    unsigned char doZmp[] = {255, 253, 93};
    assert(::write(fds[1], doZmp, sizeof(doZmp)) == static_cast<ssize_t>(sizeof(doZmp)));
    conn.pollLines();
    assert(std::get<int64_t>(harness.vm.callFunction(obj, "has", {}).data) == 1);

    harness.vm.callFunction(obj, "send_it", {});
    std::string wired = readAvailable(fds[1]);
    // "zmp.ping\0" (9) + "a\0" (2) + "b\0" (2) - the non-string 7 is
    // filtered out entirely, not sent as a stray empty field.
    assert(wired.find(std::string("zmp.ping\0a\0b\0", 13)) != std::string::npos);

    harness.vm.popCommandGiver();
    ::close(fds[1]);
    std::cout << "testZmpEfunsUseRealSignaturesAndFilterNonStringArgs OK\n";
}

// The real mudlib apply dispatch: zmp_command(string command, mixed
// *args), fired once per incoming message via
// Server::dispatchIncomingZmp() (Server.hpp's own comment: pulled out
// static for exactly this direct-test reason, mirroring
// fireMspEnableIfNegotiated()'s own precedent). No live Server/accept
// loop needed.
void testZmpCommandApplyDispatchReceivesCommandAndArgsArray() {
    NetHarness harness;
    harness.writeFile("/zmp_obj.c",
        "string last_command = \"\";\n"
        "mixed *last_args = ({});\n"
        "int call_count = 0;\n"
        "void zmp_command(string cmd, mixed *args) {\n"
        "    last_command = cmd;\n"
        "    last_args = args;\n"
        "    call_count += 1;\n"
        "}\n"
        "string get_command() { return last_command; }\n"
        "mixed *get_args() { return last_args; }\n"
        "int get_call_count() { return call_count; }\n");
    auto obj = harness.objects.cloneObject("/zmp_obj");
    assert(obj);

    int fds[2];
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    setNonBlocking(fds[0]);
    kjdmud::Connection conn(fds[0]);
    conn.attach(obj);

    std::vector<unsigned char> sb = {255, 250, 93};
    for (char c : std::string("zmp.ping\0hello\0world\0", 21)) {
        sb.push_back(static_cast<unsigned char>(c));
    }
    sb.push_back(255);
    sb.push_back(240);
    assert(::write(fds[1], sb.data(), sb.size()) == static_cast<ssize_t>(sb.size()));
    conn.pollLines();

    kjdmud::Server::dispatchIncomingZmp(harness.vm, conn);

    assert(std::get<int64_t>(harness.vm.callFunction(obj, "get_call_count", {}).data) == 1);
    assert(std::get<std::string>(harness.vm.callFunction(obj, "get_command", {}).data) == "zmp.ping");
    auto argsVal = harness.vm.callFunction(obj, "get_args", {});
    auto argsArr = std::get<std::shared_ptr<kjdmud::Array>>(argsVal.data);
    assert(argsArr->items.size() == 2);
    assert(std::get<std::string>(argsArr->items[0].data) == "hello");
    assert(std::get<std::string>(argsArr->items[1].data) == "world");

    // A second drain with nothing new queued must not re-fire.
    kjdmud::Server::dispatchIncomingZmp(harness.vm, conn);
    assert(std::get<int64_t>(harness.vm.callFunction(obj, "get_call_count", {}).data) == 1);

    ::close(fds[1]);
    std::cout << "testZmpCommandApplyDispatchReceivesCommandAndArgsArray OK\n";
}

} // namespace

// src/config/instruct.md Phase 0's own max_connections row.
void testMaxConnectionsConfigKeyDefaultsTo256AndParsesCustomValue() {
    NetHarness defaultHarness;
    assert(defaultHarness.config.maxConnections() == 256);

    NetHarness customHarness("max_connections: 3\n");
    assert(customHarness.config.maxConnections() == 3);

    std::cout << "testMaxConnectionsConfigKeyDefaultsTo256AndParsesCustomValue OK\n";
}

// Server::onNewConnection() only ever appends to connections_ once a
// new connection is fully established (see its own comment), so the
// count passed to atMaxConnections() is always "already-established
// connections", never including the one currently being processed:
// exactly at the limit must still reject, not just strictly over it.
void testAtMaxConnectionsPredicateGatesExactlyAtTheConfiguredLimit() {
    assert(!kjdmud::Server::atMaxConnections(0, 3));
    assert(!kjdmud::Server::atMaxConnections(2, 3));
    assert(kjdmud::Server::atMaxConnections(3, 3));
    assert(kjdmud::Server::atMaxConnections(4, 3));
    std::cout << "testAtMaxConnectionsPredicateGatesExactlyAtTheConfiguredLimit OK\n";
}

// MSSP (telnet option 70): this driver's own proactive "IAC WILL MSSP"
// (Server::onNewConnection()) plus the client's "IAC DO MSSP" reply is
// exactly the negotiation shape GMCP already uses; the one-shot flag
// and the actual data block are what this test covers directly, no
// live Server/accept loop needed.
void testMsspNegotiationSetsOneShotFlagAndSendMsspWritesExpectedBlock() {
    int fds[2];
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    setNonBlocking(fds[0]);
    kjdmud::Connection conn(fds[0]);

    unsigned char doMssp[] = {255, 253, 70};
    assert(::write(fds[1], doMssp, sizeof(doMssp)) == static_cast<ssize_t>(sizeof(doMssp)));
    auto lines = conn.pollLines();
    assert(lines.empty());
    assert(conn.takeMsspNegotiated());
    assert(!conn.takeMsspNegotiated());

    conn.sendMssp("kjdmud", 3, 120);
    std::string wired = readAvailable(fds[1]);
    assert(wired.size() > 5);
    assert(static_cast<unsigned char>(wired[0]) == 255);
    assert(static_cast<unsigned char>(wired[1]) == 250);
    assert(static_cast<unsigned char>(wired[2]) == 70);
    assert(static_cast<unsigned char>(wired[wired.size() - 2]) == 255);
    assert(static_cast<unsigned char>(wired[wired.size() - 1]) == 240);
    assert(wired.find("NAME") != std::string::npos);
    assert(wired.find("kjdmud") != std::string::npos);
    assert(wired.find("PLAYERS") != std::string::npos);
    assert(wired.find("UPTIME") != std::string::npos);
    assert(wired.find("120") != std::string::npos);
    assert(wired.find("CODEBASE") != std::string::npos);

    ::close(fds[1]);
    std::cout << "testMsspNegotiationSetsOneShotFlagAndSendMsspWritesExpectedBlock OK\n";
}

void runNetTests() {
    testListenConfigSynthesizesTelnetFromPort();
    testListenConfigParsesMultipleKindsAndTls();
    testSysNetworkPortsReadsTheListenTable();
    testSaveObjectWritesFluffosTextAndSecondProcessRestores();
    testSaveObjectWidthGreaterThanOneWritesAndRestoresExtraColumns();
    testSaveObjectObjectAndClosureSlotsWriteEmptyAndRestoreZero();
    testWebSocketHandshakeAndTextFrame();
    testTlsSocketpairRoundTrip();
    testEncodingAndGmcpEfunsOnASocketpair();
    testGmcpSubnegotiationIsQueued();
    testMaxConnectionsConfigKeyDefaultsTo256AndParsesCustomValue();
    testAtMaxConnectionsPredicateGatesExactlyAtTheConfiguredLimit();
    testMsspNegotiationSetsOneShotFlagAndSendMsspWritesExpectedBlock();
    testEncodingAndMsdpEfunsOnASocketpair();
    testMsdpSubnegotiationIsQueued();
    testMsdpDoReplySetsEnabledFlag();
    testMxpEfunsWrapTextOnlyWhenEnabled();
    testMxpWillNegotiationRepliesDoAndSetsEnabledFlag();
    testMxpDoReplySetsEnabledFlag();
    testMspEfunsWireBytesAndSilentNoopWhenDisabled();
    testMspDoReplySetsEnabledFlagsOnce();
    testMspEnableFiresMudlibApplyOnce();
    testZmpWillNegotiationRepliesDoAndSetsEnabledFlag();
    testZmpDoReplySetsEnabledFlag();
    testZmpSendFramingEscapesIacAndWorksEvenWithoutNegotiation();
    testZmpInboundParsingQueuesCommandAndArgs();
    testZmpInboundParsingHandlesCommandWithNoArgs();
    testZmpMalformedInboundFrameNotEndingInNulIsDiscarded();
    testZmpEfunsUseRealSignaturesAndFilterNonStringArgs();
    testZmpCommandApplyDispatchReceivesCommandAndArgsArray();
}
