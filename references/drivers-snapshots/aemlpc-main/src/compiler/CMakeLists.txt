add_library(compiler
    Lexer.cpp
    Parser.cpp
    CodeGen.cpp
)
target_include_directories(compiler PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(compiler PUBLIC core vm)
