cmake_minimum_required(VERSION 3.12)
project(BeGin LANGUAGES CXX)

# Configure C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Source files for the Host Application
set(HOST_SOURCES
    src/main.cpp
    src/App.h
    src/App.cpp
    src/MainWindow.h
    src/MainWindow.cpp
    src/EmptyView.h
    src/EmptyView.cpp
    src/WarningBanner.h
    src/WarningBanner.cpp
)

# Header files for IDE visualization
set(SDK_HEADERS
    sdk/BaseModule.h
)

# Executable target
add_executable(BeGin ${HOST_SOURCES} ${SDK_HEADERS})

# Include directories
target_include_directories(BeGin PRIVATE src sdk)

# Find native Haiku libraries
find_library(BE_LIBRARY be REQUIRED)
find_library(TRANSLATION_LIBRARY translation REQUIRED)

# Link native libraries
target_link_libraries(BeGin PRIVATE ${BE_LIBRARY} ${TRANSLATION_LIBRARY})
