# Cross-platform build driver. Every knob is a variable, so one set of recipes covers
# every build tree; the tree itself is build/<platform>/<build_type> (conanfile.py
# pins that shape in layout()).
#
#   make                                    # Linux native, Debug
#   make run                                # ... and then run it
#   make run-gdb                            # ... under gdb (linux-x86_64 only)
#   make run PLATFORM=windows-x86_64        # the cross build, under wine
#   make build PLATFORM=windows-x86_64 BUILD_TYPE=Release
#   make deps  PLATFORM=windows-x86_64      # conan install only
#   make clean                              # rm -rf build/
#
# The platforms, and the preset each one selects:
#
#   linux-x86_64    profiles/linux    ->  cmake --preset linux-x86_64-debug
#   windows-x86_64  profiles/mingw64  ->  cmake --preset windows-x86_64-debug
#
# Preset names carry the platform because CMakeUserPresets.json includes every
# platform's generated presets at once, and CMake rejects duplicate preset names.

PLATFORM   ?= linux-x86_64
BUILD_TYPE ?= Debug

# Parallelism for both halves of a build: conan passes it to every dependency it
# compiles (tools.build:jobs) and bakes it into the generated CMake preset, and the
# build step overrides the preset with it explicitly. Capped rather than left at nproc
# because the binding constraint on a desktop is RAM, not cores -- 32 concurrent C++
# translation units on a 15 GB machine is what makes the whole session stutter, and a
# first-time `--build=missing` per platform is the only step that ever needs it.
# `make JOBS=$(nproc)` when the machine is otherwise idle.
JOBS ?= 8

# Which slangc builds the shaders. Empty (the default) means whatever is on PATH, which
# is right on a machine with a complete Slang install. Set it when the one on PATH is
# incomplete -- a Release build needs slangc's spirv-opt shim, which some packages omit
# entirely (see the comment at the top of shaders/CMakeLists.txt):
#   make SLANGC=~/.local/share/nvim/mason/packages/slang/bin/slangc BUILD_TYPE=Release
SLANGC ?=

# Target -> host conan profile. The build profile is always `default`: that describes
# the machine doing the compiling, which stays Linux even when the host is Windows.
PROFILE_linux-x86_64   := profiles/linux
PROFILE_windows-x86_64 := profiles/mingw64
HOST_PROFILE := $(PROFILE_$(PLATFORM))

ifndef HOST_PROFILE
$(error no host profile for PLATFORM=$(PLATFORM); known platforms: linux-x86_64 windows-x86_64)
endif

PRESET    := $(PLATFORM)-$(shell echo $(BUILD_TYPE) | tr '[:upper:]' '[:lower:]')
BUILD_DIR := build/$(PLATFORM)/$(BUILD_TYPE)
CONAN_TOOLCHAIN := $(BUILD_DIR)/generators/conan_toolchain.cmake
CONAN_BUILD_PROFILE := $(or $(CONAN_HOME),$(HOME)/.conan2)/profiles/default

# Windows binaries cannot be executed by Linux, but wine runs them, which is the only
# way to exercise the cross build without rebooting. Inside the gitignored build/ so
# `make clean` takes it along with everything else, and deliberately not ~/.wine: a
# throwaway prefix for a build artifact should not share a C: drive with whatever else
# the machine uses wine for. wine insists this be an absolute path.
WINE_PREFIX ?= $(CURDIR)/build/wine-prefix
WINE_ENV    := WINEPREFIX=$(WINE_PREFIX) WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml="

.PHONY: all deps configure build run run-gdb run-pbr clean \
        build-debug run-debug install-debug-deps install-debug-deps-windows

all: build

# --build=missing builds whatever the local cache has no binary for, which is every
# dependency the first time a platform is built.
deps $(CONAN_TOOLCHAIN): conanfile.py $(HOST_PROFILE) $(wildcard $(CONAN_BUILD_PROFILE) conan.lock)
	conan install . -pr:h $(HOST_PROFILE) -pr:b default --build=missing -s build_type=$(BUILD_TYPE) -c tools.build:jobs=$(JOBS)

# Bootstrap a fresh tree; subsequent builds let Ninja/CMake detect configuration
# changes themselves. Keep explicit configure and deps targets for manual refreshes.
# An explicit SLANGC override must reach the cache even in an existing tree.
configure $(BUILD_DIR)/CMakeCache.txt: | $(CONAN_TOOLCHAIN)
	cmake --preset $(PRESET) $(if $(SLANGC),-DSLANGC_EXECUTABLE=$(SLANGC))

build: $(CONAN_TOOLCHAIN) $(if $(SLANGC),configure,$(BUILD_DIR)/CMakeCache.txt)
	cmake --build --preset $(PRESET) -j $(JOBS)

# `make run` runs whichever platform was asked for, because running the Windows build
# under wine is the whole point of having it.
#
# Two things in the invocation are load-bearing: shaders/shader.spv is opened relative
# to the working directory, so it cd's into the build tree first, and the Linux side
# sources the conan run env, which is what puts the conan validation layer on
# VK_LAYER_PATH -- startup fails without it, with a "layer not found" error that looks
# like the layer is missing when it is not.
#
# The Windows side has the mirror-image problem: profiles/mingw64 deliberately does not
# build the layers (see conanfile.py), and a plain wine prefix has none either, so a
# Debug exe gets as far as instance creation and stops. LC1_NO_VALIDATION=1 is the
# switch main.cpp already has for exactly this, and it is left to the caller to set:
# silently disabling the safety net on every wine run is how it stops being one.
run: build
	@case "$(PLATFORM)" in \
	linux-x86_64) \
		cd $(BUILD_DIR) && . ./generators/conanrun.sh && ./lc1 ;; \
	windows-x86_64) \
		command -v wine >/dev/null || { echo "run: wine is not installed"; exit 1; }; \
		if [ ! -d $(WINE_PREFIX) ]; then \
			echo "run: creating $(WINE_PREFIX) (first time only)"; \
			$(WINE_ENV) wineboot -u >/dev/null 2>&1; \
		fi; \
		echo "run: if this stops at 'layer not available', use LC1_NO_VALIDATION=1 make run PLATFORM=$(PLATFORM)"; \
		cd $(BUILD_DIR) && $(WINE_ENV) wine ./lc1.exe ;; \
	*) \
		echo "run: unknown platform $(PLATFORM)"; exit 1 ;; \
	esac

# Same run, under gdb. Linux only: the wine build has no debugger worth pretending to
# here. The conan env is sourced before exec for the same reason `run` needs it -- the
# inferior must find the conan libraries, and gdb must find their debug info -- and
# `exec` keeps Ctrl-C reaching gdb instead of a wrapper shell. Debug builds carry the
# symbols; Release runs with a mostly-empty backtrace, which is left to the caller.
run-gdb: build
	@case "$(PLATFORM)" in \
	linux-x86_64) \
		cd $(BUILD_DIR) && . ./generators/conanrun.sh && exec gdb ./lc1 ;; \
	*) \
		echo "run-gdb: cannot debug PLATFORM=$(PLATFORM); only linux-x86_64 runs natively"; exit 1 ;; \
	esac

# Compatibility alias: the showcase now lives in the main game scene.
run-pbr: run

clean:
	rm -rf build/

# Kept so muscle memory and older notes keep working. The parameterised targets above
# are the real interface now.
build-debug:
	$(MAKE) BUILD_TYPE=Debug build

run-debug:
	$(MAKE) BUILD_TYPE=Debug run

install-debug-deps:
	$(MAKE) BUILD_TYPE=Debug deps

install-debug-deps-windows:
	$(MAKE) PLATFORM=windows-x86_64 BUILD_TYPE=Debug deps
