cmake_minimum_required(VERSION 4.2)
project(OSFinal C)

set(CMAKE_C_STANDARD 11)

add_executable(OSFinal main.c
        utils.h
        setup.c
        data.csv
        simulation_thread.c)
