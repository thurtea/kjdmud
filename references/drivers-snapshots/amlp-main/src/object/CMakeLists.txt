add_library(object
    LivingNameRegistry.cpp
    LiveObjectRegistry.cpp
    LpcObject.cpp
    ObjectManager.cpp
)
target_include_directories(object PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(object PUBLIC core compiler vm security)
