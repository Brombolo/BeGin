cmake_minimum_required(VERSION 3.12)
project(TaskManager LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compile as a shared module (add-on library)
add_library(TaskManager MODULE TaskManagerModule.cpp)

# Include the BeGin SDK path relative to this directory
target_include_directories(TaskManager PRIVATE ../../sdk)

# Find and link Haiku native system libraries (libbe.so)
find_library(BE_LIBRARY be REQUIRED)
target_link_libraries(TaskManager PRIVATE ${BE_LIBRARY})

# Strip "lib" prefix to compile directly as TaskManager.so
set_target_properties(TaskManager PROPERTIES PREFIX "")
