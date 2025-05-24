cmake_minimum_required(VERSION 3.10)
project(webserver)

# set standard
set(CMAKE_CXX_STANDARD 11)

#
# find_package(PkgConfig REQUIRED)
# if(PKG_CONFIG_FOUND)
#     pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavutil)
# else()
#     message(STATUS "libavutil starnot found!")
# endif()
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/module;${CMAKE_MODULE_PATH}")
add_executable(ffmpeg_log ffmpeg_log.cpp)


find_package(FFMPEG REQUIRED)
if(FFMPEG_FOUND)
    target_link_libraries(ffmpeg_log PRIVATE ${FFMPEG_LIBRARY})
    target_include_directories(ffmpeg_log PUBLIC ${FFMPEG_INCLUDE_DIRS})
else(FFMPEG_FOUND)
    message(STATUS "libavutil not found!")
endif(FFMPEG_FOUND)

