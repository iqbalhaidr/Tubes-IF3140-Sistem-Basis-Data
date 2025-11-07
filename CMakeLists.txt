cmake_minimum_required(VERSION 3.15)
project(mDBMS_Project CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Tambahkan direktori /include ke path header global
include_directories(include)

# Tambahkan subdirektori src dan tests
add_subdirectory(src)
add_subdirectory(tests)
