add_library(net
    Connection.cpp
    Server.cpp
    OutputContext.cpp
    InteractiveRegistry.cpp
    SocketRegistry.cpp
    SnoopRelay.cpp
)
target_include_directories(net PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(net PUBLIC core vm object config)
