# MinGW-w64 build (also works as a cross-compile from Linux).
# On Windows (MSYS2/MinGW) run: make CROSS=
CROSS ?= x86_64-w64-mingw32-
CXX := $(CROSS)g++
CC := $(CROSS)gcc
WINDRES := $(CROSS)windres
OFFICIAL ?= 0
CXXFLAGS = -DCUE_OFFICIAL_BUILD=$(OFFICIAL)  -MMD -MP -std=c++20 -O2 -flto -ffunction-sections -fdata-sections -Wall -Wextra -Wno-missing-field-initializers -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -D_WIN32_WINNT=0x0A00 -DNOMINMAX
LDFLAGS = -flto=2 -Wl,--gc-sections -municode -mwindows -static -static-libgcc -static-libstdc++ -s
LIBS = -ldwmapi -lole32 -lshell32 -luuid -lshcore -lcomdlg32 -lwinmm -lwindowscodecs -lwinhttp -lbcrypt -lws2_32 -liphlpapi -lcrypt32 -lwintrust
SRC = $(wildcard src/core/*.cpp) $(wildcard src/ui/*.cpp) $(wildcard src/win/*.cpp)
OBJ = $(patsubst src/%.cpp,build/obj/%.o,$(SRC)) build/obj/res.o

VERSION := $(shell sed -n 's/^\#define CUE_VERSION "\(.*\)"/\1/p' src/core/version.h)

all: build/Cue/Cue.exe

# Optional console diagnostic, kept outside the installed application.
preview-benchmark: build/cue-preview-bench.exe

build/cue-preview-bench.exe: tools/preview_benchmark.cpp src/core/preview_protocol.h src/core/preview_timeline.h
	@mkdir -p build
	$(CXX) -std=c++20 -O2 -Wall -Wextra -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -DNOMINMAX -municode -static -s $< -o $@

.PHONY: FORCE
build/identity: FORCE
	@mkdir -p build
	@echo $(OFFICIAL) | cmp -s - $@ || echo $(OFFICIAL) > $@

$(OBJ) $(SETUP_OBJ): build/identity src/core/build_identity.h

build/obj/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/obj/res.o: res/cue.rc res/cue.ico res/cue-audio.ico res/cue.manifest src/core/version.h
	@mkdir -p $(dir $@)
	$(WINDRES) -DCUE_OFFICIAL_BUILD=$(OFFICIAL) -I res res/cue.rc -O coff -o $@.tmp
	mv $@.tmp $@

build/Cue/Cue.exe: $(OBJ)
	@mkdir -p build/Cue/fonts
	$(CXX) $(OBJ) -o $@ $(LDFLAGS) $(LIBS)
	cp assets/fonts/*.ttf build/Cue/fonts/
	mkdir -p build/Cue/licenses && cp assets/fonts/*.txt build/Cue/licenses/
	cp third_party/fusion-pixel-font/OFL.txt build/Cue/licenses/FusionPixel-OFL.txt
	cp docs/LICENSES.md build/Cue/LICENSES.md
	cp docs/ASSETS.md build/Cue/ASSETS.md
	cp docs/SECURITY.md build/Cue/SECURITY.md
	mkdir -p build/Cue/sources && cp res/sources-README.txt build/Cue/sources/README.txt
	cp LICENSE COPYRIGHT.md NOTICE.md docs/BRANDING.md docs/VALIDATION.md build/Cue/
	mkdir -p build/Cue/docs && cp docs/*.md build/Cue/docs/
	mkdir -p build/Cue/third_party && cp -r third_party/libmpv build/Cue/third_party/

# ── installer ──────────────────────────────────────────────────────
# make setup: build/Cue must hold libmpv-2.dll (tools/fetch-libmpv.ps1 or the 1.0 package)
SETUP_SRC = installer/setup.cpp installer/setup_ui.cpp src/ui/gfx.cpp src/ui/face.cpp src/core/release.cpp src/core/json.cpp \
            src/core/i18n.cpp src/win/ui_language.cpp
SETUP_OBJ = $(patsubst %.cpp,build/setup/obj/%.o,$(SETUP_SRC)) build/setup/obj/LzmaDec.o build/setup/obj/setup_res.o
$(SETUP_OBJ): build/identity src/core/build_identity.h
SETUP_LIBS = -ldwmapi -lole32 -lshell32 -luuid -lshcore -lgdi32 -luser32 -ladvapi32

build/setup/obj/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/setup/obj/LzmaDec.o: installer/lzma/LzmaDec.c
	@mkdir -p $(dir $@)
	$(CC) -O2 -flto -c $< -o $@

build/setup/obj/setup_res.o: installer/setup.rc installer/setup.manifest res/cue.ico src/core/version.h
	@mkdir -p $(dir $@)
	$(WINDRES) -DCUE_OFFICIAL_BUILD=$(OFFICIAL) -I installer installer/setup.rc -O coff -o $@

build/setup/stub.exe: $(SETUP_OBJ)
	$(CXX) $(SETUP_OBJ) -o $@ $(LDFLAGS) $(SETUP_LIBS)

setup: build/Cue/Cue.exe build/setup/stub.exe
	@# (the documents again: they were copied only when Cue.exe was linked, so an edit to them
	@# alone reached the installer late)
	cp docs/LICENSES.md build/Cue/LICENSES.md && cp docs/ASSETS.md build/Cue/ASSETS.md && cp docs/SECURITY.md build/Cue/SECURITY.md
	cp res/sources-README.txt build/Cue/sources/README.txt
	cp LICENSE COPYRIGHT.md NOTICE.md docs/BRANDING.md docs/VALIDATION.md build/Cue/ && cp docs/*.md build/Cue/docs/
	mkdir -p build/Cue/third_party && cp -r third_party/libmpv build/Cue/third_party/
	python3 tools/make_setup.py build/Cue build/setup/stub.exe dist/Cue-$(VERSION)-Setup.exe

clean:
	rm -rf build

# ── translations ───────────────────────────────────────────────────
# i18n/*.json -> src/core/strings.h + src/core/i18n_catalog.inc (checked in, like assets.inc)
i18n:
	python3 tools/gen_i18n.py

.PHONY: all clean setup i18n

-include $(OBJ:.o=.d) $(SETUP_OBJ:.o=.d)
