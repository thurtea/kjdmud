# src/proto/ - GMCP, MSDP, MSSP, MTTS, MXP Protocol Handlers (Phase 3)

## Purpose

Out-of-band MUD protocols layered on top of telnet subnegotiation. These
protocols let modern MUD clients (Mudlet, BlowTorch, MUSHclient) display
rich UI - character sheets, maps, health bars, sound - without cluttering
the main text stream.

This is a **Phase 3 directory**. It depends on the telnet IAC infrastructure
built in `src/net` Phase 0.8.

## Protocols to implement

| Protocol | Option code | Purpose |
|----------|-------------|---------|
| GMCP | 201 (0xC9) | Generic Mud Communication Protocol: JSON key-value pairs |
| MSDP | 69 (0x45) | Mud Server Data Protocol: structured telnet variables |
| MSSP | 70 (0x46) | Mud Server Status Protocol: server info for crawlers |
| MTTS | 24 (0x18) | Mud Terminal Type Standard: client capability flags |
| MXP | 91 (0x5B) | MUD eXtension Protocol: rich HTML-like text markup |

## Files to create

### `include/amlp/proto/GmcpHandler.hpp`

```cpp
class GmcpHandler {
public:
    // Called by Connection when it receives IAC SB GMCP <data> IAC SE.
    void onReceive(Connection& conn, const std::string& package,
                   const nlohmann::json& data);

    // Send a GMCP message to a connection.
    static void send(Connection& conn, const std::string& package,
                     const nlohmann::json& data);

    // Register a mudlib GMCP callback via set_gmcp_callback(fn).
    void registerCallback(std::shared_ptr<Closure> fn);
};
```

GMCP messages have the form `"Package.Name"` + optional JSON body.
Example: `"Char.Vitals" {"hp": 100, "maxhp": 100}`.

### `include/amlp/proto/MsdpHandler.hpp`

MSDP uses TELNET variables (IAC SB MSDP MSDP_VAR name MSDP_VAL value IAC SE).

```cpp
class MsdpHandler {
public:
    void onReceive(Connection& conn, const std::string& varName,
                   const std::string& value);
    static void send(Connection& conn, const std::string& varName,
                     const std::string& value);
    static void sendTable(Connection& conn, const std::string& varName,
                          const std::vector<std::pair<std::string,std::string>>& table);
};
```

### `include/amlp/proto/MsspHandler.hpp`

MSSP is server-to-client only (for crawlers). Sends static server info once
on connection: server name, uptime, player count, etc.

```cpp
class MsspHandler {
public:
    // Send MSSP variables to a newly connected client that supports MSSP.
    static void sendServerInfo(Connection& conn, const Config& config,
                               int playerCount, int uptimeSeconds);
};
```

### `include/amlp/proto/MttsHandler.hpp`

Parses the MTTS terminal type string to extract client capability flags:
- `MTTS_ANSI` (bit 0)
- `MTTS_VT100` (bit 1)
- `MTTS_UTF8` (bit 2)
- `MTTS_256COLORS` (bit 3)
- `MTTS_MOUSETRACKING` (bit 4)
- `MTTS_OSC` (bit 5)
- `MTTS_SCREENREADER` (bit 6)
- `MTTS_PROXY` (bit 7)
- `MTTS_TRUECOLOR` (bit 8)
- `MTTS_MNES` (bit 9)
- `MTTS_MSLP` (bit 10)

### `include/amlp/proto/MxpHandler.hpp`

MXP sends tagged text (`<B>`, `<A href="...">`, `<send>`, etc.) that MXP-
capable clients render as rich UI. Non-MXP clients must receive plain text.

```cpp
class MxpHandler {
public:
    // Enable/disable MXP on a connection (negotiated via IAC DO MXP).
    static void setEnabled(Connection& conn, bool enabled);

    // Wrap text in an MXP element if the connection supports MXP;
    // otherwise return plain text.
    static std::string link(Connection& conn, const std::string& text,
                             const std::string& command);
    static std::string bold(Connection& conn, const std::string& text);
    static std::string color(Connection& conn, const std::string& text,
                              const std::string& fg, const std::string& bg);
};
```

## New efuns (register in `src/efun`)

- `gmcp_send(object player, string package, mapping data)` - send GMCP
- `msdp_send(object player, string var, mixed value)` - send MSDP
- `query_gmcp(object player)` → 1 if client supports GMCP
- `query_msdp(object player)` → 1 if client supports MSDP
- `query_client_flags(object player)` → int MTTS bitmask
- `query_mxp(object player)` → 1 if MXP enabled
- `mxp_tag(object player, string tag, string text)` → string (MXP-wrapped or plain)
- `set_gmcp_callback(function fn)` - register mudlib handler for incoming GMCP

## Integration in `src/net`

`Connection` gains:
- `bool gmcpEnabled_ = false;`
- `bool msdpEnabled_ = false;`
- `bool msspEnabled_ = false;`
- `int mttsFlags_ = 0;`
- `bool mxpEnabled_ = false;`
- `std::unique_ptr<GmcpHandler> gmcp_`
- `std::unique_ptr<MsdpHandler> msdp_`

`Connection::processTelnetOption()` routes option codes 69/70/24/91/201 to
the appropriate handler.

## CMakeLists.txt

```cmake
add_library(proto STATIC
    GmcpHandler.cpp
    MsdpHandler.cpp
    MsspHandler.cpp
    MttsHandler.cpp
    MxpHandler.cpp
)
target_include_directories(proto PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(proto PUBLIC net config)
target_link_libraries(amlp PRIVATE proto)
```

## Testing

`test/test_proto.cpp`:
- GMCP: send subneg bytes → assert `onReceive()` called with correct package+data
- MSDP: encode a table value → assert correct binary byte sequence
- MSSP: `sendServerInfo()` → assert output contains `"PLAYERS"` variable
- MTTS: parse `"MTTS 271"` → assert correct bitmask flags
- MXP: `bold()` with MXP enabled → assert `<B>text</B>` output
- MXP: `bold()` with MXP disabled → assert plain `text` output
