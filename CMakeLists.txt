cmake_minimum_required(VERSION 4.1)
project(OSProject C)

set(CMAKE_C_STANDARD 99)

add_executable(city_manager main.c)
add_executable(monitor_reports monitor_reports.c)
