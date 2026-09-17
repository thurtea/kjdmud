add_library(vm
    VM.cpp
    Value.cpp
)
target_include_directories(vm PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(vm PUBLIC core)
