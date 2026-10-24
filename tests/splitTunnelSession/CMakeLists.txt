cmake_minimum_required(VERSION 3.21)
project(SplitTunnelSessionTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 REQUIRED COMPONENTS Core Network)
add_executable(splitTunnelSessionTests main.cpp)
target_include_directories(splitTunnelSessionTests PRIVATE ../../client)
target_link_libraries(splitTunnelSessionTests PRIVATE Qt6::Core Qt6::Network)
enable_testing()
add_test(NAME splitTunnelSession COMMAND splitTunnelSessionTests)

set(CMAKE_AUTOMOC ON)
add_executable(localSocketDisconnectTests localSocketDisconnect.cpp
    ../../client/mozilla/localsocketcontroller.cpp
    ../../client/mozilla/localsocketcontroller.h
    ../../client/mozilla/controllerimpl.h)
target_include_directories(localSocketDisconnectTests PRIVATE stubs ../../client ../../client/mozilla/shared)
target_link_libraries(localSocketDisconnectTests PRIVATE Qt6::Core Qt6::Network)
add_test(NAME localSocketDisconnect COMMAND localSocketDisconnectTests)
