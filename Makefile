# Thin wrapper over CMakePresets.json, so building, testing and cleaning don't
# mean remembering cmake/ctest flags. `make help` lists the targets.
#
# PRESET picks the CMake preset (default: the host's release preset), e.g.
#   make test PRESET=windows-debug
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

.DEFAULT_GOAL := help
.PHONY: help configure build test clean distclean format format-check

help:
	@echo Targets (PRESET=$(PRESET), override with PRESET=windows-debug, linux-san, ...):
	@echo   configure     cmake --preset
	@echo   build         configure, then compile
	@echo   test          build, then run ctest
	@echo   clean         remove build outputs, keep the configuration
	@echo   distclean     delete $(BUILD_DIR)
	@echo   format        clang-format -i on src, tests and tools
	@echo   format-check  the same check CI runs (no changes written)

configure:
	$(RUN) cmake --preset $(PRESET)

build: configure
	$(RUN) cmake --build --preset $(PRESET)

test: build
	$(RUN) ctest --preset $(PRESET)

clean:
	$(RUN) cmake --build --preset $(PRESET) --target clean

distclean:
	cmake -E rm -rf $(BUILD_DIR)

format:
	clang-format -i $(shell git ls-files -- $(CXX_SOURCES))

format-check:
	clang-format --dry-run --Werror $(shell git ls-files -- $(CXX_SOURCES))
