cmake_minimum_required(VERSION 3.20)
project(itch-feed-handler CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Release build by default
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native")
set(CMAKE_CXX_FLAGS_DEBUG   "-O0 -g -fsanitize=address,undefined")

include_directories(include)

# core library — parser and order book
add_library(itch_core
  src/order_book.cpp
  src/itch_parser.cpp
)

# sender executable
add_executable(itch_sender
  src/itch_sender.cpp
)
target_link_libraries(itch_sender itch_core)

# receiver executable
add_executable(baseline_receiver
  src/baseline_receiver.cpp
)
target_link_libraries(baseline_receiver itch_core)