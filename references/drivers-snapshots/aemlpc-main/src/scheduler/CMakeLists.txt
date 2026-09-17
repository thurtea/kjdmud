add_library(scheduler
    Scheduler.cpp
)
target_include_directories(scheduler PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(scheduler PUBLIC core vm net)
