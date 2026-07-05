cmake_minimum_required(VERSION 3.10)
project(UberRidePlatform CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(src)
include_directories(third_party/Crow/include)
include_directories(third_party/asio/include)

# Main.cpp pulls in the class files for this teaching/demo build.
add_executable(UberRidePlatform Main.cpp)
target_link_libraries(UberRidePlatform sqlite3)

if (WIN32)
    target_link_libraries(UberRidePlatform ws2_32 mswsock)
endif()
