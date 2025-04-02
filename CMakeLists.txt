cmake_minimum_required(VERSION 3.10)

project(Dateisystem VERSION 1.0)

set(CMAKE_CXX_STANDARD 11)

include_directories(${PROJECT_SOURCE_DIR}/src)

add_executable(dateisystem.bin src/main.c)