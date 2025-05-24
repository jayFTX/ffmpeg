CXX = g++
CFLAGS := -g -Wall

SRCS := $(wildcard *.cpp)
TARGETS := $(basename $(SRCS))
LD_FLAGS := $(shell pkg-config sdl3 libswscale libswresample libavcodec libavutil libavformat --libs)
INC_DIR := $(shell pkg-config sdl3 libswscale libswresample libavcodec libavutil libavformat --cflags)
# LD_FLAGS += -lpthread

$(info $(SRCS))
$(info $(TARGETS))

.PHONY: all clean
all: $(TARGETS)

%: %.cpp
	$(CXX) $< -o $@ $(INC_DIR) $(LD_FLAGS) $(CFLAGS)  

clean: 
	$(RM) $(TARGETS)

	