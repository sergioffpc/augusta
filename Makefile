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

# What CI's clang-tidy step lints: every src .cpp except the two Windows-only
# trees, which its Linux build graph has no compile commands for.
TIDY_EXCLUDES := ":(exclude)src/client/*" ":(exclude)src/modules/renderer/*"
ifeq ($(OS),Windows_NT)
# PhysX's SSE headers break clang-tidy under MSVC's flags, so this one is
# linted by CI's Linux run alone.
TIDY_EXCLUDES += ":(exclude)src/modules/physics/physics.cpp"
endif
TIDY_SOURCES := $(shell git ls-files -- "src/*.cpp" $(TIDY_EXCLUDES))

.DEFAULT_GOAL := help
.PHONY: help configure build test clean distclean format format-check tidy lint

help:
	@echo Targets (PRESET=$(PRESET), override with PRESET=windows-debug, linux-san, ...):
	@echo   configure     cmake --preset
	@echo   build         configure, then compile
	@echo   test          build, then run ctest
	@echo   clean         remove build outputs, keep the configuration
	@echo   distclean     delete $(BUILD_DIR)
	@echo   format        clang-format -i on src, tests and tools
	@echo   format-check  the same check CI runs (no changes written)
	@echo   tidy          clang-tidy on src, as CI runs it (configures first)
	@echo   lint          format-check, then tidy: everything CI lints

configure:
	$(RUN) cmake --preset $(PRESET)

build: configure
	$(RUN) cmake --build --preset $(PRESET)

test: build
	$(RUN) ctest --preset $(PRESET)

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
