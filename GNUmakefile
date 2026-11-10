# GNUmakefile - Setup98 on macOS / Linux (GNU make picks this file; NMAKE on Windows uses Makefile):
#   make            bin/pack98 (native packer, byte-identical output to bin\pack98.exe)
#   make test       strict -std=c++98 -Wall -Wextra -Werror + ASan/UBSan: codec, manifest and pack98 tests
#   make fuzz       longer codec fuzz run (FUZZ=n mutations per file, default 20000)
#   make CXX=g++    another compiler (default: clang++ if installed, else g++); SAN= turns the sanitizers off
# The tests need nothing outside this repository: tests/gensample.cpp writes a deterministic sample release folder
# (BMP, WAV, text, incompressible binary). If the author's sibling release folders exist (../sandline, ../editor),
# their files are tested too.
# The stub bin/setup98.exe is built by VC6 on Windows (nmake); it ships prebuilt. Tests use it, else a dummy.
ifeq ($(origin CXX),default)
CXX := $(shell command -v clang++ >/dev/null 2>&1 && echo clang++ || echo g++)
endif
WARN = -std=c++98 -pedantic -Wall -Wextra -Wshadow -Werror
SAN = -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all
COMMON = src/manifest.cpp src/wild.cpp src/crc32.cpp src/lzhdec.cpp
HDRS = $(wildcard src/*.h)
M = obj/mac
FUZZ = 20000
SAMPLE = $(M)/sample
# optional extra samples: real release files of two projects that use Setup98, when they sit next to this repo
EXTRA = $(wildcard ../sandline/release/RetroStrike/art/*.bmp ../sandline/release/RetroStrike/sfx/explode.wav \
          ../sandline/release/RetroStrike/RETROSTR.EXE ../sandline/release/RetroStrike/README.TXT \
          ../editor/release/Pica-1.0/pica.exe)
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

$(M)/gensample: tests/gensample.cpp $(HDRS)
	@mkdir -p $(M)
	$(CXX) $(WARN) -O1 -o $@ tests/gensample.cpp

$(SAMPLE)/GAME.EXE: $(M)/gensample
	rm -rf $(SAMPLE)
	$(M)/gensample $(SAMPLE)

$(M)/dummystub.exe:
	@mkdir -p $(M)
	@printf 'MZ' > $@ && head -c 4094 /dev/zero >> $@

GEN = $(SAMPLE)/GAME.EXE $(SAMPLE)/README.TXT $(SAMPLE)/art/sky.bmp $(SAMPLE)/art/tiles.bmp $(SAMPLE)/art/odd.bmp \
      $(SAMPLE)/sfx/tone.wav $(SAMPLE)/sfx/chord.wav $(SAMPLE)/data/levels/level1.lvl examples/demo/s98demo.exe \
      examples/demo/data/logo.bmp

test: bin/pack98 $(M)/codectest $(M)/mantest $(M)/pack98-san $(M)/dummystub.exe $(SAMPLE)/GAME.EXE
	$(M)/codectest -q -fuzz 300 $(GEN) $(EXTRA)
	$(M)/mantest tests/manifests/*.ini
	sh tests/packtest.sh $(M)/pack98-san $(STUB)
	@echo "all tests passed$(if $(EXTRA),, (sibling release folders not present: their samples were skipped))"

fuzz: $(M)/codectest $(SAMPLE)/GAME.EXE
	$(M)/codectest -q -fuzz $(FUZZ) -seed 7 $(GEN) $(EXTRA)
	$(M)/codectest -q -fuzz $(FUZZ) -seed 99 $(SAMPLE)/README.TXT $(SAMPLE)/art/sky.bmp

# compression ratios of the sample files (no -q: one line per file)
ratios: $(M)/codectest $(SAMPLE)/GAME.EXE
	$(M)/codectest -fuzz 0 $(GEN) $(EXTRA)

clean:
	rm -rf $(M) bin/pack98 out/test

.PHONY: all test fuzz ratios clean
