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

find_package(Qt6 REQUIRED COMPONENTS Gui Qml Quick)
set(CMAKE_AUTORCC ON)
add_executable(splitTunnelGroupsUiSmoke groupsUiSmoke.cpp ../../client/images/images.qrc)
target_link_libraries(splitTunnelGroupsUiSmoke PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick)
get_filename_component(TEST_REPO_PATH "../.." ABSOLUTE)
target_compile_definitions(splitTunnelGroupsUiSmoke PRIVATE REPO_PATH="${TEST_REPO_PATH}")
add_test(NAME splitTunnelGroupsUiSmoke COMMAND splitTunnelGroupsUiSmoke -platform offscreen)

add_executable(splitTunnelCompatibilityTests compatibility.cpp)
target_include_directories(splitTunnelCompatibilityTests PRIVATE ../../client)
target_link_libraries(splitTunnelCompatibilityTests PRIVATE Qt6::Core Qt6::Network)
add_test(NAME splitTunnelCompatibility COMMAND splitTunnelCompatibilityTests)
