cmake_minimum_required(VERSION 3.27)
project(fat12conv VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)

add_executable(fat12conv main.c)

install(TARGETS fat12conv)
