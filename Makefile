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
# On Windows every command runs through scripts\vcenv.cmd, which loads the
# Visual Studio Build Tools environment first (cl.exe needs it, see README).
# GNU make for Windows: `winget install ezwinports.make`.

ifeq ($(OS),Windows_NT)
SHELL := cmd.exe
.SHELLFLAGS := /c
PRESET ?= windows
RUN := scripts\vcenv.cmd
else
PRESET ?= linux
RUN :=
endif

# Every preset's binaryDir is build/x64-<preset> (CMakePresets.json).
BUILD_DIR := build/x64-$(PRESET)

CXX_SOURCES := "src/*.cpp" "src/*.h" "tests/*.cpp" "tests/*.h" "tools/*.cpp" "tools/*.h"
LUA_SOURCES := "*.lua"
TOML_SOURCES := "*.toml" ":!:third_party/*"
PYTHON_SOURCES := "*.py" ":!:third_party/*"
SHELL_SOURCES := "*.sh" ".githooks/*"
CMAKE_SOURCES := "CMakeLists.txt" "*/CMakeLists.txt" "*.cmake" ":!:third_party/*"
MARKDOWN_SOURCES := "*.md" ":!:third_party/*" ":!:CHANGELOG.md"

# The formatters and linters uv runs, at the versions CI pins.
RUFF := uv tool run ruff@0.16.10
SHFMT := uv tool run --from shfmt-py==4.2.0 shfmt
SHELLCHECK := uv tool run --from shellcheck-py==0.11.0.1 shellcheck
ACTIONLINT := uv tool run --from actionlint-py==1.7.12.25 --with shellcheck-py==0.11.0.1 actionlint
GERSEMI := uv tool run gersemi@0.29.2
PYMARKDOWN := uv tool run --from pymarkdownlnt==0.9.40 pymarkdown --config .pymarkdown.json
# Prettier is a Node package: uv runs Node from its PyPI wheel, and npx Prettier.
PRETTIER := uv tool run --from nodejs-wheel==24.19.0 npx --yes prettier@3.9.9

# What CI's Lint step runs clang-tidy on: every src .cpp except the two
# Windows-only trees and the audio module's Windows-only output device, which
# its Linux build graph has no compile commands for, and the C++ tools'
# (tools/swarm), built with them.
TIDY_EXCLUDES := ":(exclude)src/client/*" ":(exclude)src/modules/renderer/*"
ifeq ($(OS),Windows_NT)
# PhysX's SSE headers break clang-tidy under MSVC's flags, so this one is
# linted by CI's Linux run alone, as is the audio output every other build has.
TIDY_EXCLUDES += ":(exclude)src/modules/physics/physics.cpp" ":(exclude)src/modules/audio/output_none.cpp"
else
TIDY_EXCLUDES += ":(exclude)src/modules/audio/output_miniaudio.cpp" ":(exclude)src/modules/audio/miniaudio.cpp"
endif
TIDY_SOURCES := $(shell git ls-files -- "src/*.cpp" "tools/swarm/*.cpp" ":(exclude)tools/*/tests/*" $(TIDY_EXCLUDES))

# The rest of src and tests that this platform's build has compile commands
# for, which tidy holds to include-cleaner alone (.clang-tidy's
# misc-include-cleaner): the tests and, on Windows, the Windows-only sources.
# -w because the presets compile with -Werror (/WX), which turns warnings in
# third-party headers into errors that cut the parse short and leave includes
# looking unused.
ifeq ($(OS),Windows_NT)
INCLUDE_EXCLUDES := ":(exclude)src/modules/physics/physics.cpp" ":(exclude)src/modules/audio/output_none.cpp"
JOBS ?= $(NUMBER_OF_PROCESSORS)
else
INCLUDE_EXCLUDES := $(TIDY_EXCLUDES) ":(exclude)tests/client_*"
JOBS ?= $(shell nproc)
endif
INCLUDE_SOURCES := $(filter-out $(TIDY_SOURCES),$(shell git ls-files -- "src/*.cpp" "tests/*.cpp" "tools/swarm/tests/*.cpp" $(INCLUDE_EXCLUDES)))
# One target per file, so a parallel make runs them side by side.
TIDY_CHECKS := $(addprefix tidy/,$(TIDY_SOURCES))
INCLUDE_CHECKS := $(addprefix include-cleaner/,$(INCLUDE_SOURCES))

.DEFAULT_GOAL := all
.PHONY: all help configure build test check coverage install uninstall clean distclean format format-check tidy lint docs

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
	$(info $()  coverage      test under the linux-coverage preset, then its report into build/x64-linux-coverage/report)
	$(info $()  install       build, then cmake --install augustad (prefix=..., DESTDIR=...))
	$(info $()  uninstall     remove what install put in place (same DESTDIR))
	$(info $()  clean         remove build outputs, keep the configuration)
	$(info $()  distclean     delete $(BUILD_DIR))
	$(info $()  format        every formatter (C++, YAML, Lua, TOML, Python, shell, CMake, Markdown, PowerShell) on tracked files)
	$(info $()  format-check  every formatter check and linter CI's format job runs (not clang-tidy))
	$(info $()  tidy          clang-tidy on src, include-cleaner on what it leaves out, as CI runs them (configures first))
	$(info $()  lint          format-check, then tidy: everything CI lints)
	$(info $()  docs          the documentation site, MkDocs and Doxygen, into build/docs-site)
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

# The tests' coverage (ADR-0013), whatever PRESET says: the linux-coverage
# preset's tests, then their llvm-cov report. The last run's profiles go first,
# so the report is of this run alone.
coverage:
	cmake -E rm -rf build/x64-linux-coverage/profiles
	$(MAKE) --no-print-directory test PRESET=linux-coverage
	bash scripts/coverage-report.sh

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

# The documentation site (ADR-0046): MkDocs first, since it empties the site
# folder, then Doxygen's API reference into its api/ folder.
docs:
	uv run --locked --project tools --only-group docs mkdocs build --strict
	doxygen tools/docs/Doxyfile

format:
	clang-format -i $(shell git ls-files -- $(CXX_SOURCES))
	yamlfmt -conf .yamlfmt
	stylua $(shell git ls-files -- $(LUA_SOURCES))
	taplo fmt $(shell git ls-files -- $(TOML_SOURCES))
	$(RUFF) format $(shell git ls-files -- $(PYTHON_SOURCES))
	$(SHFMT) -w $(shell git ls-files -- $(SHELL_SOURCES))
	$(GERSEMI) -i $(shell git ls-files -- $(CMAKE_SOURCES))
	$(PRETTIER) --log-level warn --write $(shell git ls-files -- $(MARKDOWN_SOURCES))

format-check:
	clang-format --dry-run --Werror $(shell git ls-files -- $(CXX_SOURCES))
	yamlfmt -conf .yamlfmt -lint
	uv tool run --from yamllint==1.37.1 yamllint --strict -c .yamllint .
	stylua --check $(shell git ls-files -- $(LUA_SOURCES))
	luacheck $(shell git ls-files -- $(LUA_SOURCES))
	taplo fmt --check $(shell git ls-files -- $(TOML_SOURCES))
	taplo lint $(shell git ls-files -- $(TOML_SOURCES))
	$(RUFF) format --check $(shell git ls-files -- $(PYTHON_SOURCES))
	$(RUFF) check $(shell git ls-files -- $(PYTHON_SOURCES))
	$(SHFMT) -d $(shell git ls-files -- $(SHELL_SOURCES))
	$(SHELLCHECK) $(shell git ls-files -- $(SHELL_SOURCES))
	$(ACTIONLINT)
	$(GERSEMI) --check $(shell git ls-files -- $(CMAKE_SOURCES))
	$(PRETTIER) --log-level warn --check $(shell git ls-files -- $(MARKDOWN_SOURCES))
	$(PYMARKDOWN) scan $(shell git ls-files -- $(MARKDOWN_SOURCES))

# Configuring first keeps
# compile_commands.json in step with the sources (a file new on a branch has no
# entry until then).
tidy: configure
	$(MAKE) --no-print-directory -j $(JOBS) -k -Otarget $(TIDY_CHECKS) $(INCLUDE_CHECKS)

.PHONY: $(TIDY_CHECKS) $(INCLUDE_CHECKS)
$(TIDY_CHECKS): tidy/%:
	$(RUN) clang-tidy -p=$(BUILD_DIR) $*

$(INCLUDE_CHECKS): include-cleaner/%:
	$(RUN) clang-tidy --quiet -p=$(BUILD_DIR) --checks=-*,misc-include-cleaner --extra-arg=-w $*

lint: format-check tidy
