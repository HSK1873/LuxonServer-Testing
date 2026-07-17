cmake_minimum_required(VERSION 3.16)

project(basiccoro
    VERSION 1.0.0
    DESCRIPTION "A very basic C++ coroutine library"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


add_library(basiccoro STATIC
    include/SingleEvent.hpp src/SingleEvent.cpp
    include/AwaitableTask.hpp
    include/FfiAwaiter.hpp
)

target_include_directories(basiccoro
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)


include(GNUInstallDirs)

install(TARGETS basiccoro
    EXPORT basiccoro-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/basiccoro
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
