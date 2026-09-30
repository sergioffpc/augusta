# Thin wrapper over CMakePresets.json, so building, testing and cleaning don't
# mean remembering cmake/ctest flags. `make help` lists the targets.
#
# PRESET picks the CMake preset (default: the host's release preset), e.g.
#   make test PRESET=windows-debug
#
# Plain `make` builds, and the targets follow the GNU names (all, check,
# install, uninstall, clean, distclean). `install` honours the GNU `prefix`
# (CMake's default when unset) and DESTDIR, e.g.
#   make install prefix=/opt/augusta DESTDIR=/tmp/stage
#
# On Windows every command runs through scripts\vcenv.ps1, which loads the
# Visual Studio Build Tools environment first (cl.exe needs it, see README).
# GNU make for Windows: `winget install ezwinports.make`.

ifeq ($(OS),Windows_NT)
SHELL := cmd.exe
.SHELLFLAGS := /c
PRESET ?= windows
RUN := powershell -NoProfile -ExecutionPolicy Bypass -File scripts\vcenv.ps1
else
PRESET ?= linux
RUN :=
endif

# Every preset's binaryDir is build/x64-<preset> (CMakePresets.json).
BUILD_DIR := build/x64-$(PRESET)

CXX_SOURCES := "src/*.cpp" "src/*.h" "tests/*.cpp" "tests/*.h" "tools/*.cpp" "tools/*.h"

# What CI's clang-tidy step lints: every src .cpp except the two Windows-only
# trees, which its Linux build graph has no compile commands for.
TIDY_EXCLUDES := ":(exclude)src/client/*" ":(exclude)src/modules/renderer/*"
ifeq ($(OS),Windows_NT)
# PhysX's SSE headers break clang-tidy under MSVC's flags, so this one is
# linted by CI's Linux run alone.
TIDY_EXCLUDES += ":(exclude)src/modules/physics/physics.cpp"
endif
TIDY_SOURCES := $(shell git ls-files -- "src/*.cpp" $(TIDY_EXCLUDES))

.DEFAULT_GOAL := all
.PHONY: all help configure build test check install uninstall clean distclean format format-check tidy lint

all: build

# $(info), not echo: make prints it itself, so the parentheses reach neither
# cmd.exe nor /bin/sh, which would read them as syntax.
help:
	$(info Targets (PRESET=$(PRESET), override with PRESET=windows-debug, linux-san, ...):)
	$(info $()  all           the default: build)
	$(info $()  configure     cmake --preset)
	$(info $()  build         configure, then compile the binaries (no tests))
	$(info $()  test          build, then compile the tests and run ctest)
	$(info $()  check         the same as test)
	$(info $()  install       build, then cmake --install augustad (prefix=..., DESTDIR=...))
	$(info $()  uninstall     remove what install put in place (same DESTDIR))
	$(info $()  clean         remove build outputs, keep the configuration)
	$(info $()  distclean     delete $(BUILD_DIR))
	$(info $()  format        clang-format -i on src, tests and tools)
	$(info $()  format-check  the same check CI runs (no changes written))
	$(info $()  tidy          clang-tidy on src, as CI runs it (configures first))
	$(info $()  lint          format-check, then tidy: everything CI lints)
	@:

configure:
	$(RUN) cmake --preset $(PRESET)

build: configure
	$(RUN) cmake --build --preset $(PRESET)

# The tests are not part of build (ADR-0008): augusta_tests builds them.
test: build
	$(RUN) cmake --build --preset $(PRESET) --target augusta_tests
	$(RUN) ctest --preset $(PRESET)

check: test

# DESTDIR reaches cmake --install through the environment: make exports a
# variable set on its command line. PREFIX is taken for the GNU prefix too.
prefix ?= $(PREFIX)
install: build
	$(RUN) cmake --install $(BUILD_DIR) $(if $(prefix),--prefix $(prefix))

# CMake has no uninstall; cmake/Uninstall.cmake removes what the install
# manifest lists. A staged install needs the same DESTDIR here.
uninstall:
	$(if $(wildcard $(BUILD_DIR)/install_manifest.txt),cmake -DMANIFEST=$(BUILD_DIR)/install_manifest.txt -P cmake/Uninstall.cmake,@echo Nothing to uninstall: $(BUILD_DIR) has no install manifest.)

# Nothing to clean before the first configure (or after distclean): the clean
# target only exists inside a configured build tree.
clean:
	$(if $(wildcard $(BUILD_DIR)),$(RUN) cmake --build --preset $(PRESET) --target clean,@echo Nothing to clean: $(BUILD_DIR) does not exist.)

distclean:
	cmake -E rm -rf $(BUILD_DIR)

format:
	clang-format -i $(shell git ls-files -- $(CXX_SOURCES))

format-check:
	clang-format --dry-run --Werror $(shell git ls-files -- $(CXX_SOURCES))

# -p is written -p=<dir> because PowerShell reads a bare -p as its own
# -PipelineVariable when vcenv.ps1 forwards the arguments, and clang-tidy would
# then run without the compile commands. Configuring first keeps
# compile_commands.json in step with the sources (a file new on a branch has no
# entry until then).
tidy: configure
	$(RUN) clang-tidy -p=$(BUILD_DIR) $(TIDY_SOURCES)

lint: format-check tidy
