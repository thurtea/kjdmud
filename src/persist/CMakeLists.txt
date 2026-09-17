add_library(persist
    StateSerializer.cpp
)
target_include_directories(persist PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(persist PUBLIC core object vm)
