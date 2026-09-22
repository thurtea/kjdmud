add_library(proto
    GmcpHandler.cpp
    MsdpHandler.cpp
    MxpHandler.cpp
)
target_include_directories(proto PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(proto PUBLIC net)
