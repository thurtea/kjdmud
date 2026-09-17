add_library(apply
    ApplyTable.cpp
)
target_include_directories(apply PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(apply PUBLIC core)
