#include "amlp/config/Config.hpp"
#include "amlp/efun/EfunTable.hpp"
#include "amlp/net/Connection.hpp"
#include "amlp/net/InteractiveRegistry.hpp"
#include "amlp/net/OutputContext.hpp"
#include "amlp/object/ObjectManager.hpp"
#include "amlp/vm/VM.hpp"
#include "amlp/vm/Value.hpp"

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
    amlp::Config config;
    amlp::ObjectManager objects;
    amlp::VM vm;

    explicit NetHarness(const std::string& extraConfigLines = "")
        : objects(config), vm(objects, config) {
        objects.setVM(&vm);
        char dirTemplate[] = "/tmp/amlp_net_test_XXXXXX";
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
    assert(ports[0].kind == amlp::ListenKind::Telnet);
    assert(ports[0].port == 0);
    assert(!ports[0].tls);
    assert(ports[0].slot == 1);
    std::cout << "testListenConfigSynthesizesTelnetFromPort OK\n";
}

void testListenConfigParsesMultipleKindsAndTls() {
    NetHarness harness("listen: telnet 2222\nlisten: websocket 8080\nlisten: telnet 8443 tls\n");
    const auto& ports = harness.config.listenPorts();
    assert(ports.size() == 3);
    assert(ports[0].kind == amlp::ListenKind::Telnet && ports[0].port == 2222 && !ports[0].tls);
    assert(ports[1].kind == amlp::ListenKind::WebSocket && ports[1].port == 8080 && !ports[1].tls);
    assert(ports[2].kind == amlp::ListenKind::Telnet && ports[2].port == 8443 && ports[2].tls);
    std::cout << "testListenConfigParsesMultipleKindsAndTls OK\n";
}

void testSysNetworkPortsReadsTheListenTable() {
    NetHarness harness("listen: telnet 2222\nlisten: websocket 8080 tls\n");
    harness.writeFile("/ports.c", "mixed *probe() { return sys_network_ports(); }\n");
    auto ob = harness.objects.cloneObject("/ports");
    assert(ob);
    amlp::Value result = harness.vm.callFunction(ob, "probe", {});
    auto outer = std::get<std::shared_ptr<amlp::Array>>(result.data);
    assert(outer && outer->items.size() == 2);
    auto a = std::get<std::shared_ptr<amlp::Array>>(outer->items[0].data);
    auto b = std::get<std::shared_ptr<amlp::Array>>(outer->items[1].data);
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
    amlp::Value saved = writer.vm.callFunction(obj, "save", {});
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

void testWebSocketHandshakeAndTextFrame() {
    int fds[2];
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    setNonBlocking(fds[0]);
    amlp::Connection conn(fds[0]);
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
    char dirTemplate[] = "/tmp/amlp_tls_XXXXXX";
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
    amlp::Connection conn(fds[0]);
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
    amlp::Connection conn(fds[0]);
    conn.attach(obj);
    harness.vm.pushCommandGiver(obj);

    amlp::Value enc = harness.vm.callFunction(obj, "get_it", {});
    assert(std::get<std::string>(enc.data) == "utf-8");
    assert(std::get<int64_t>(harness.vm.callFunction(obj, "set_it", {amlp::Value(std::string("iso-8859-1"))}).data) == 1);
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
    amlp::Connection conn(fds[0]);
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

} // namespace

void runNetTests() {
    testListenConfigSynthesizesTelnetFromPort();
    testListenConfigParsesMultipleKindsAndTls();
    testSysNetworkPortsReadsTheListenTable();
    testSaveObjectWritesFluffosTextAndSecondProcessRestores();
    testWebSocketHandshakeAndTextFrame();
    testTlsSocketpairRoundTrip();
    testEncodingAndGmcpEfunsOnASocketpair();
    testGmcpSubnegotiationIsQueued();
}
