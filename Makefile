# "make CROSS=1" to compile for windows on linux
# can also "make" directly on windows if clang and some cygwin tools (i.e. make, rm) are in the path

ifneq ($(MAKECMDGOALS),nodlsym)
	CFLAGS := -DSHARED $(CFLAGS)
	SID :=
else
	SID := s
endif

nodlsym: all

CROSS ?= 0

ifneq ($(CROSS), 0)
	OS := Windows_NT
endif

ifeq ($(OS),Windows_NT)
	CC := winegcc
	DETECTED_OS := Windows
	TARGET_SUFFIX := -windows-msvc
	EXE_EXT := .exe
	RM := rm -f
	LDFLAGS := -Wl,/SAFESEH:NO $(LDFLAGS)
	SHARED_LIB_FLAGS := -shared -Wl,--image-base,0x180000000 -Wl,-dynamicbase:no -Wl,--section-alignment,4096 -mllvm -align-all-nofallthru-blocks=9
	SHARED_LIB_EXT := .dll
else
	CC := clang
	DETECTED_OS := $(shell uname -s)
	TARGET_SUFFIX := -linux-gnu
	EXE_EXT :=
	RM := rm -f
	SHARED_LIB_FLAGS := -shared -fno-PIC -Wl,--section-start=.text=0x1000 -mllvm -align-all-nofallthru-blocks=9
	SHARED_LIB_EXT := .so
endif

ARCH ?= native

BASE_FLAGS := -Wall -Wextra -pedantic -march=$(ARCH) -mtune=$(ARCH) -std=gnu23
LINK_FLAGS := -fuse-ld=lld $(LDFLAGS)

TARGET_64 := x86_64-pc$(TARGET_SUFFIX)
TARGET_32 := i386-pc$(TARGET_SUFFIX)

RELEASE_FLAGS := $(BASE_FLAGS) -O3 $(CFLAGS)
ASAN_FLAGS := $(BASE_FLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer -O1

FLAGS_64 := $(RELEASE_FLAGS) --target=$(TARGET_64)
FLAGS_32 := $(RELEASE_FLAGS) --target=$(TARGET_32)
ASAN_FLAGS_64 := $(ASAN_FLAGS) --target=$(TARGET_64)
ASAN_FLAGS_32 := $(ASAN_FLAGS) --target=$(TARGET_32)

ifeq ($(DETECTED_OS),Windows)
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

SHARED_LIBS := libmembase64$(TARGET_SUFFIX)$(SHARED_LIB_EXT) libmembase32$(TARGET_SUFFIX)$(SHARED_LIB_EXT)
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
	@echo "32-bit target: $(TARGET_32)"
ifneq ($(MAKECMDGOALS),nodlsym)
	@echo "Benchmarking shared libraries."
endif

ifneq ($(MAKECMDGOALS),nodlsym)
MEMBASE_OBJS64 :=
MEMBASE_OBJS32 :=
bench: $(SHARED_LIBS) membench64$(SID)$(EXE_EXT) membench32$(SID)$(EXE_EXT)
else
MEMBASE_OBJS64 := membase64$(TARGET_SUFFIX)$(SID).o
MEMBASE_OBJS32 := membase32$(TARGET_SUFFIX)$(SID).o
bench: membench64$(SID)$(EXE_EXT) membench32$(SID)$(EXE_EXT)
endif

membench64$(SID)$(EXE_EXT): $(BENCH_SOURCES) $(MEMBASE_OBJS64)
	$(CC) $(FLAGS_64) -o $@ $^ $(MATH_LIB) $(LINK_FLAGS)

membench32$(SID)$(EXE_EXT): $(BENCH_SOURCES) $(MEMBASE_OBJS32)
	$(CC) $(FLAGS_32) -o $@ $^ $(MATH_LIB) $(LINK_FLAGS)

# .sos/.dlls
libmembase64$(TARGET_SUFFIX)$(SHARED_LIB_EXT): $(BASE_SOURCES)
	$(CC) $(FLAGS_64) $(SHARED_LIB_FLAGS) -o $@ $< $(MATH_LIB) $(LINK_FLAGS)

libmembase32$(TARGET_SUFFIX)$(SHARED_LIB_EXT): $(BASE_SOURCES)
	$(CC) $(FLAGS_32) $(SHARED_LIB_FLAGS) -o $@ $< $(MATH_LIB) $(LINK_FLAGS)

# testing (linux only) (always "static")
test: memtest64$(EXE_EXT) memtest32$(EXE_EXT)

memtest64$(EXE_EXT): $(TEST_SOURCES) membase64$(TARGET_SUFFIX)$(SID).o
	$(CC) $(FLAGS_64) -o $@ $^ $(MATH_LIB) $(LINK_FLAGS)

memtest32$(EXE_EXT): $(TEST_SOURCES) membase32$(TARGET_SUFFIX)$(SID).o
	$(CC) $(FLAGS_32) -o $@ $^ $(MATH_LIB) $(LINK_FLAGS)

# ASAN test (linux only)
asan: memtest64_asan$(EXE_EXT) memtest32_asan$(EXE_EXT)

memtest64_asan$(EXE_EXT): $(TEST_SOURCES) membase64_asan$(TARGET_SUFFIX)$(SID).o
	$(CC) $(ASAN_FLAGS_64) -o $@ $^ $(MATH_LIB) $(LINK_FLAGS)

memtest32_asan$(EXE_EXT): $(TEST_SOURCES) membase32_asan$(TARGET_SUFFIX)$(SID).o
	$(CC) $(ASAN_FLAGS_32) -o $@ $^ $(MATH_LIB) $(LINK_FLAGS)


membase64$(TARGET_SUFFIX)$(SID).o: $(BASE_SOURCES)
	$(CC) $(FLAGS_64) -o $@ -c $<

membase32$(TARGET_SUFFIX)$(SID).o: $(BASE_SOURCES)
	$(CC) $(FLAGS_32) -o $@ -c $<

membase64_asan$(TARGET_SUFFIX)$(SID).o: $(BASE_SOURCES)
	$(CC) $(ASAN_FLAGS_64) -o $@ -c $<

membase32_asan$(TARGET_SUFFIX)$(SID).o: $(BASE_SOURCES)
	$(CC) $(ASAN_FLAGS_32) -o $@ -c $<

clean:
	$(RM) $(ALL_BINS) *.o *.a *.so *.dll *.dylib *.dSYM
