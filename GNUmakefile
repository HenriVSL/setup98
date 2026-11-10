# GNUmakefile - Setup98 on the Mac (GNU make picks this file; NMAKE in Win98 uses Makefile):
#   make            bin/pack98 (Mac packer, byte-identical output to bin\pack98.exe)
#   make test       strict clang -Wall -Wextra -Werror + ASan/UBSan: codec, manifest and pack98 tests
#   make fuzz       longer codec fuzz run (FUZZ=n mutations per file, default 20000)
# The stub bin/setup98.exe is built by VC6 in Win98 (w98 build setup98); tests use it when present, else a dummy.
CXX = clang++
WARN = -std=c++98 -pedantic -Wall -Wextra -Wshadow -Werror
SAN = -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all
COMMON = src/manifest.cpp src/wild.cpp src/crc32.cpp src/lzhdec.cpp
HDRS = $(wildcard src/*.h)
M = obj/mac
FUZZ = 20000
SAMPLES = $(wildcard ../sandline/release/RetroStrike/art/*.bmp) ../sandline/release/RetroStrike/sfx/explode.wav \
          ../sandline/release/RetroStrike/RETROSTR.EXE ../sandline/release/RetroStrike/README.TXT
STUB = $(if $(wildcard bin/setup98.exe),bin/setup98.exe,$(M)/dummystub.exe)

all: bin/pack98

bin/pack98: src/pack98.cpp src/lzhenc.cpp $(COMMON) $(HDRS)
	@mkdir -p bin
	$(CXX) $(WARN) -O2 -o $@ src/pack98.cpp src/lzhenc.cpp $(COMMON)

$(M)/pack98-san: src/pack98.cpp src/lzhenc.cpp $(COMMON) $(HDRS)
	@mkdir -p $(M)
	$(CXX) $(WARN) $(SAN) -o $@ src/pack98.cpp src/lzhenc.cpp $(COMMON)

$(M)/codectest: tests/codectest.cpp src/lzhenc.cpp src/lzhdec.cpp src/crc32.cpp $(HDRS)
	@mkdir -p $(M)
	$(CXX) $(WARN) -Wno-deprecated-declarations $(SAN) -o $@ tests/codectest.cpp src/lzhenc.cpp src/lzhdec.cpp src/crc32.cpp

$(M)/mantest: tests/mantest.cpp $(COMMON) $(HDRS)
	@mkdir -p $(M)
	$(CXX) $(WARN) $(SAN) -o $@ tests/mantest.cpp $(COMMON)

$(M)/dummystub.exe:
	@mkdir -p $(M)
	@printf 'MZ' > $@ && head -c 4094 /dev/zero >> $@

test: bin/pack98 $(M)/codectest $(M)/mantest $(M)/pack98-san $(M)/dummystub.exe
	$(M)/codectest -q -fuzz 300 $(SAMPLES)
	$(M)/mantest tests/manifests/*.ini
	sh tests/packtest.sh $(M)/pack98-san $(STUB)
	@echo "all Mac tests passed"

fuzz: $(M)/codectest
	$(M)/codectest -q -fuzz $(FUZZ) -seed 7 $(SAMPLES)
	$(M)/codectest -q -fuzz $(FUZZ) -seed 99 ../editor/release/Pica-1.0/pica.exe

clean:
	rm -rf $(M) bin/pack98 out/test

.PHONY: all test fuzz clean
