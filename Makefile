# "make CROSS=1" to compile for windows on linux
# can also "make" directly on windows if clang and some cygwin tools (i.e. make, rm) are in the path

CROSS ?= 0
MUSL ?= 0
SID := s

ifneq ($(MAKECMDGOALS),nodlsym)
CFLAGS := -DSHARED $(CFLAGS)
SID :=
endif

nodlsym: all

ifneq ($(CROSS), 0)
OS := Windows_NT
endif

ARCH ?= native

ifeq ($(OS),Windows_NT)
CC := winegcc
DETECTED_OS := Windows
TARGET_SUFFIX := -windows-msvc
EXE_EXT := .exe
RM := rm -f
LDFLAGS := -Wl,/SAFESEH:NO $(LDFLAGS)
SHARED_LIB_FLAGS64 := -shared -Wl,--image-base,0x180000000 -Wl,-dynamicbase:no -Wl,--section-alignment,4096 -mllvm -align-all-nofallthru-blocks=9
SHARED_LIB_FLAGS32 := $(SHARED_LIB_FLAGS64)
SHARED_LIB_EXT := .dll
else ifneq ($(MUSL),0)
CC := musl-clang
DETECTED_OS := $(shell uname -s)
TARGET_SUFFIX := -linux-musl
EXE_EXT :=
RM := rm -f
CFLAGS := -DMUSL -Wno-unused-command-line-argument $(CFLAGS)
SHARED_LIB_FLAGS64 := -shared -fno-PIC -Wl,--section-start=.text=0x1000 -mllvm -align-all-nofallthru-blocks=9
SHARED_LIB_FLAGS32 := $(SHARED_LIB_FLAGS64)
SHARED_LIB_EXT := .so
else
CC := clang
DETECTED_OS := $(shell uname -s)
TARGET_SUFFIX := -linux-gnu
EXE_EXT :=
RM := rm -f
SHARED_LIB_FLAGS64 := -shared -Wl,--section-start=.text=0x1000 -mllvm -align-all-nofallthru-blocks=9 -fno-PIC
SHARED_LIB_FLAGS32 := $(SHARED_LIB_FLAGS64) -fPIC
SHARED_LIB_EXT := .so
endif

TARGET_64 := x86_64$(TARGET_SUFFIX)
TARGET_32 := i386$(TARGET_SUFFIX)

BASE_FLAGS := -Wall -Wextra -pedantic -std=gnu23 -march=$(ARCH) -mtune=$(ARCH) $(CFLAGS)
LINK_FLAGS := -fuse-ld=lld -fno-plt $(LDFLAGS)

RELEASE_FLAGS := -O3 $(BASE_FLAGS)
ASAN_FLAGS := $(BASE_FLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer -O1

FLAGS_64 := $(RELEASE_FLAGS) --target=$(TARGET_64)
FLAGS_32 := $(RELEASE_FLAGS) --target=$(TARGET_32)
ASAN_FLAGS_64 := $(ASAN_FLAGS) --target=$(TARGET_64)
ASAN_FLAGS_32 := $(ASAN_FLAGS) --target=$(TARGET_32)

ifeq ($(DETECTED_OS),Windows)
	MATH_LIB :=
else ifneq ($(MUSL),0)
	MATH_LIB :=
else
	MATH_LIB := -lm
endif

TEST_SOURCES := memtest.c
BENCH_SOURCES := membench.c
BASE_SOURCES := membase.c membase.h

ifeq ($(DETECTED_OS),Windows)
ASAN_BINS :=
TEST_BINS :=
else
ASAN_BINS := memtest64_asan memtest32_asan
TEST_BINS := memtest64 memtest32
endif

ifeq ($(MUSL),0)
SHARED_LIBS := libmembase64$(TARGET_SUFFIX)$(SHARED_LIB_EXT) libmembase32$(TARGET_SUFFIX)$(SHARED_LIB_EXT)
else
SHARED_LIBS := libmembase64$(TARGET_SUFFIX)$(SHARED_LIB_EXT)
endif
BENCH_BINS := membench64s membench64 membench32s membench32 membench64s.exe membench64.exe membench32s.exe membench32.exe
ALL_BINS := $(BENCH_BINS) $(TEST_BINS) $(ASAN_BINS) $(SHARED_LIBS)

ifeq ($(DETECTED_OS),Windows)
.PHONY: all clean bench info
all: bench
else
.PHONY: all clean bench test asan info
all: bench test
endif

info:
	@echo "Detected OS: $(DETECTED_OS)"
	@echo "Target suffix: $(TARGET_SUFFIX)"
	@echo "64-bit target: $(TARGET_64)"
ifeq ($(MUSL),0)
	@echo "32-bit target: $(TARGET_32)"
endif

ifneq ($(MAKECMDGOALS),nodlsym)
MEMBASE_OBJS64 :=
MEMBASE_OBJS32 :=
ifeq ($(MUSL),0)
bench: $(SHARED_LIBS) membench64$(SID)$(EXE_EXT) membench32$(SID)$(EXE_EXT)
else
bench: $(SHARED_LIBS) membench64$(SID)$(EXE_EXT)
endif
else
MEMBASE_OBJS64 := membase64$(TARGET_SUFFIX)$(SID).o
MEMBASE_OBJS32 := membase32$(TARGET_SUFFIX)$(SID).o
ifeq ($(MUSL),0)
bench: membench64$(SID)$(EXE_EXT) membench32$(SID)$(EXE_EXT)
else
bench: membench64$(SID)$(EXE_EXT)
endif
endif

ifeq ($(MUSL),0)
test: memtest64$(EXE_EXT) memtest32$(EXE_EXT)
asan: memtest64_asan$(EXE_EXT) memtest32_asan$(EXE_EXT)
endif

membench64$(SID)$(EXE_EXT): $(BENCH_SOURCES) $(MEMBASE_OBJS64)
	$(CC) $(FLAGS_64) -o $@ $^ $(MATH_LIB) $(LINK_FLAGS)

# .sos/.dlls
libmembase64$(TARGET_SUFFIX)$(SHARED_LIB_EXT): $(BASE_SOURCES)
	$(CC) $(FLAGS_64) $(SHARED_LIB_FLAGS64) -o $@ $< $(MATH_LIB) $(LINK_FLAGS)

# testing (linux only) (always "static")
memtest64$(EXE_EXT): $(TEST_SOURCES) membase64$(TARGET_SUFFIX)$(SID).o
	$(CC) $(FLAGS_64) -o $@ $^ $(MATH_LIB) $(LINK_FLAGS)

# ASAN test (linux only)
memtest64_asan$(EXE_EXT): $(TEST_SOURCES) membase64_asan$(TARGET_SUFFIX)$(SID).o
	$(CC) $(ASAN_FLAGS_64) -o $@ $^ $(MATH_LIB) $(LINK_FLAGS)

membase64$(TARGET_SUFFIX)$(SID).o: $(BASE_SOURCES)
	$(CC) $(FLAGS_64) -o $@ -c $<

membase64_asan$(TARGET_SUFFIX)$(SID).o: $(BASE_SOURCES)
	$(CC) $(ASAN_FLAGS_64) -o $@ -c $<

ifeq ($(MUSL),0) # no 32bit musl in arch repos? "zig cc" had other problems, like seemingly not being able to dynamically link...

membench32$(SID)$(EXE_EXT): $(BENCH_SOURCES) $(MEMBASE_OBJS32)
	$(CC) $(FLAGS_32) -o $@ $^ $(MATH_LIB) $(LINK_FLAGS)

libmembase32$(TARGET_SUFFIX)$(SHARED_LIB_EXT): $(BASE_SOURCES)
	$(CC) $(FLAGS_32) $(SHARED_LIB_FLAGS32) -o $@ $< $(MATH_LIB) $(LINK_FLAGS)

memtest32$(EXE_EXT): $(TEST_SOURCES) membase32$(TARGET_SUFFIX)$(SID).o
	$(CC) $(FLAGS_32) -o $@ $^ $(MATH_LIB) $(LINK_FLAGS)

memtest32_asan$(EXE_EXT): $(TEST_SOURCES) membase32_asan$(TARGET_SUFFIX)$(SID).o
	$(CC) $(ASAN_FLAGS_32) -o $@ $^ $(MATH_LIB) $(LINK_FLAGS)

membase32$(TARGET_SUFFIX)$(SID).o: $(BASE_SOURCES)
	$(CC) $(FLAGS_32) -o $@ -c $<

membase32_asan$(TARGET_SUFFIX)$(SID).o: $(BASE_SOURCES)
	$(CC) $(ASAN_FLAGS_32) -o $@ -c $<

endif

clean:
	$(RM) $(ALL_BINS) *.o *.a *.so *.dll *.dylib *.dSYM
