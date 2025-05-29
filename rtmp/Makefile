CC = g++
CFLAGS := -g -Wall

SRCS := $(wildcard *.cpp)
TARGETS := $(basename $(SRCS))
LD_FLAGS := $(shell pkg-config librtmp sdl3 libswscale libswresample libavcodec libavutil libavformat --libs)
INC_DIR := $(shell pkg-config librtmp sdl3 libswscale libswresample libavcodec libavutil libavformat --cflags)

$(info $(SRCS))
$(info $(TARGETS))

.PHONY: all clean
all: $(TARGETS)

%: %.cpp
	$(CC) $(CFLAGS) $(INC_DIR) $< -o $@  $(LD_FLAGS) $(LIBS)

clean: 
	$(RM) $(TARGETS)

	