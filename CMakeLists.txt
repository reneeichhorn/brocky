cmake_minimum_required (VERSION 3.6)

project(brocky-client CXX)
set(CMAKE_CXX_STANDARD 14)

add_definitions(-DRPI_CLIENT)

include_directories(deps/quiche/include)

link_directories(deps/quiche/target/debug)

add_executable(brocky-client src/main.cpp src/quic_client.cpp)
target_link_libraries(brocky-client quiche)
#set_property(TARGET quiche PROPERTY IMPORTED_LOCATION deps/quiche/target/debug/libquiche.so)