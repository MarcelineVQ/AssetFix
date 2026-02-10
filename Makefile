# Makefile for assetfix DLL
# Cross-compiles from Linux to Windows using MinGW-w64
#
# Purpose: MPQ archive loading discovery and loose-file research

CC      = i686-w64-mingw32-gcc
CXX     = i686-w64-mingw32-g++
STRIP   = i686-w64-mingw32-strip

BUILD_DIR = build
TARGET = assetfix.dll

# Source files
CPP_SRCS = \
    dllmain.cpp \
    looseFiles.cpp

# MinHook (git submodule)
MINHOOK_DIR = minhook
MINHOOK_SRCS = \
    $(MINHOOK_DIR)/src/buffer.c \
    $(MINHOOK_DIR)/src/hook.c \
    $(MINHOOK_DIR)/src/trampoline.c \
    $(MINHOOK_DIR)/src/hde/hde32.c

# Object files
CPP_OBJS = $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(CPP_SRCS))
MINHOOK_OBJS = \
    $(BUILD_DIR)/minhook_buffer.o \
    $(BUILD_DIR)/minhook_hook.o \
    $(BUILD_DIR)/minhook_trampoline.o \
    $(BUILD_DIR)/minhook_hde32.o

ALL_OBJS = $(CPP_OBJS) $(MINHOOK_OBJS)

# Compiler flags
COMMON_FLAGS = -DUNICODE -D_UNICODE -DWIN32 -D_WIN32
COMMON_FLAGS += -I$(MINHOOK_DIR)/include
COMMON_FLAGS += -Wall
COMMON_FLAGS += -D__USE_MINGW_ANSI_STDIO=0

DEBUG_FLAGS = -g -O0 -DDEBUG
RELEASE_FLAGS = -O2 -DNDEBUG

CFLAGS   = $(COMMON_FLAGS) -std=c11
CXXFLAGS = $(COMMON_FLAGS) -std=c++17 -fpermissive

LDFLAGS  = -shared
LDFLAGS += -static -static-libgcc -static-libstdc++
LDFLAGS += -Wl,--subsystem,windows
LIBS     = -luser32 -lkernel32

.PHONY: all clean release debug check-submodule

all: debug

check-submodule:
	@if [ ! -f "$(MINHOOK_DIR)/include/MinHook.h" ]; then \
		echo "MinHook submodule not found, initializing..."; \
		git submodule update --init --recursive; \
	fi

debug: CFLAGS += $(DEBUG_FLAGS)
debug: CXXFLAGS += $(DEBUG_FLAGS)
debug: check-submodule dirs $(ALL_OBJS)
	$(CXX) $(LDFLAGS) -o $(TARGET) $(ALL_OBJS) $(LIBS)
	@echo "Built: $(TARGET)"
	@ls -lh $(TARGET) | awk '{print "Size: " $$5}'

release: CFLAGS += $(RELEASE_FLAGS)
release: CXXFLAGS += $(RELEASE_FLAGS)
release: check-submodule dirs $(ALL_OBJS)
	$(CXX) $(LDFLAGS) -o $(TARGET) $(ALL_OBJS) $(LIBS)
	$(STRIP) --strip-all $(TARGET)
	@echo "Release built: $(TARGET)"
	@ls -lh $(TARGET) | awk '{print "Size: " $$5}'

dirs:
	@mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/minhook_buffer.o: $(MINHOOK_DIR)/src/buffer.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/minhook_hook.o: $(MINHOOK_DIR)/src/hook.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/minhook_trampoline.o: $(MINHOOK_DIR)/src/trampoline.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/minhook_hde32.o: $(MINHOOK_DIR)/src/hde/hde32.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(TARGET)
