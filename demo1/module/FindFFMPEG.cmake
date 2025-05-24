message(STATUS "FFMPEG_module start!")

find_path(FFMPEG_INCLUDE_DIRS NAMES log.h PATHS /usr/include /usr/local/include)

find_library(FFMPEG_LIBRARY NAMES avutil PATHS /usr/lib /usr/local/lib)

message(STATUS "FFMPEG_INCLUDE_DIRS = ${FFMPEG_INCLUDE_DIRS}")
message(STATUS "FFMPEG_LIBRARY = ${FFMPEG_LIBRARY}")

if (FFMPEG_INCLUDE_DIRS AND FFMPEG_LIBRARY)
    set(FFMPEG_FOUND TRUE)
else()
    message(STATUS "FFMPEG_FOUND is false! ")
endif (FFMPEG_INCLUDE_DIRS AND FFMPEG_LIBRARY)

message(STATUS "FFMPEG_module end!")
