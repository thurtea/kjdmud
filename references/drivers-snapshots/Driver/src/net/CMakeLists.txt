find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSSL REQUIRED IMPORTED_TARGET libssl)
pkg_check_modules(LIBCRYPTO REQUIRED IMPORTED_TARGET libcrypto)

add_library(net
    Connection.cpp
    Server.cpp
    OutputContext.cpp
    InteractiveRegistry.cpp
    SocketRegistry.cpp
    SnoopRelay.cpp
)
target_include_directories(net PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(net PUBLIC
    core vm object config
    PkgConfig::LIBSSL
    PkgConfig::LIBCRYPTO
)
