cmake_minimum_required(VERSION 3.15)
project(MotorNeuromorficoUniversal VERSION 3.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/O2 /EHsc /W3)
else()
    add_compile_options(-O3 -Wall -Wextra -pthread)
endif()

set(SOURCE_FILES
    main.cpp
    cerebro.cpp
    server.cpp
    synthetic_signal_adapter.cpp
)

add_executable(motor_neuromorfico ${SOURCE_FILES})

if(WIN32)
    target_link_libraries(motor_neuromorfico PRIVATE ws2_32)
else()
    target_link_libraries(motor_neuromorfico PRIVATE pthread)
endif()
