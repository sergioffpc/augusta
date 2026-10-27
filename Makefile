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
# Visual Studio Build Tools environment first (clang-cl needs it, see README).
# GNU make for Windows: `winget install ezwinports.make`.

# The host's system, as uname names it; Windows has no uname.
ifeq ($(OS),Windows_NT)
HOST_OS := Windows_NT
else
HOST_OS := $(shell uname -s)
endif

ifeq ($(HOST_OS),Windows_NT)
SHELL := cmd.exe
.SHELLFLAGS := /c
PRESET ?= windows
RUN := scripts\vcenv.cmd
else ifeq ($(HOST_OS),Darwin)
PRESET ?= macos
RUN :=
else
PRESET ?= linux
RUN :=
endif

# Every preset's binaryDir is build/x64-<preset>, the macOS ones'
# build/arm64-<preset> (CMakePresets.json).
ifneq ($(filter macos%,$(PRESET)),)
BUILD_DIR := build/arm64-$(PRESET)
else
BUILD_DIR := build/x64-$(PRESET)
endif

CXX_SOURCES := "src/*.cpp" "src/*.h" "tests/*.cpp" "tests/*.h" "tools/*.cpp" "tools/*.h"
LUA_SOURCES := "*.lua"
TOML_SOURCES := "*.toml" ":!:third_party/*"
PYTHON_SOURCES := "*.py" ":!:third_party/*"
SHELL_SOURCES := "*.sh" ".githooks/*"
CMAKE_SOURCES := "CMakeLists.txt" "*/CMakeLists.txt" "*.cmake" ":!:third_party/*"
MARKDOWN_SOURCES := "*.md" ":!:third_party/*" ":!:CHANGELOG.md"
PROTO_SOURCES := "*.proto" ":!:third_party/*"
# What .yamlfmt and .yamllint cover, named file by file, so FILES narrows them
# as it does the rest.
YAML_SOURCES := "*.yaml" "*.yml" "*.yamlfmt" "*.yamllint" ":!:third_party/*" \
  ":!:charts/augustad/templates/*" ":!:clusters/onprem/flux-system/gotk-components.yaml" \
  ":!:clusters/onprem/infrastructure/kube-bench/cfg/*"

# The toolchain's formatters and linters (ADR-0077), from the checkout's
# .toolchain/bin, at the versions scripts/bootstrap/versions.env pins. Never
# another binary: without .toolchain/, a target that runs one stops and says to
# make it. Recursive, so the targets that run none run without it. cmd.exe
# reads a forward slash as an option, so Windows' paths take backslashes.
ifeq ($(wildcard .toolchain/bin),)
TOOLCHAIN_MISSING = $(error .toolchain/bin not found: run `make toolchain` first)
endif
ifeq ($(OS),Windows_NT)
toolchain_bin = $(TOOLCHAIN_MISSING).toolchain\bin\$(1)
else
toolchain_bin = $(TOOLCHAIN_MISSING).toolchain/bin/$(1)
endif
RUFF = $(call toolchain_bin,ruff)
SHFMT = $(call toolchain_bin,shfmt)
SHELLCHECK = $(call toolchain_bin,shellcheck)
# actionlint lints the workflows' run scripts with the toolchain's shellcheck.
ACTIONLINT = $(call toolchain_bin,actionlint) -shellcheck=$(call toolchain_bin,shellcheck)
GERSEMI = $(call toolchain_bin,gersemi)
PYMARKDOWN = $(call toolchain_bin,pymarkdown) --config .pymarkdown.json
YAMLLINT = $(call toolchain_bin,yamllint) --strict -c .yamllint
# LLVM's, which .toolchain/bin links to the system's of the pinned major.
CLANG_FORMAT = $(call toolchain_bin,clang-format)
# Prettier is a Node package, in .toolchain/node_modules, which the toolchain's
# Node runs.
PRETTIER = $(call toolchain_bin,node) .toolchain/node_modules/prettier/bin/prettier.cjs
YAMLFMT = $(call toolchain_bin,yamlfmt) -conf .yamlfmt
STYLUA = $(call toolchain_bin,stylua)
LUACHECK = $(call toolchain_bin,luacheck)
TAPLO = $(call toolchain_bin,taplo)
# buf reads buf.yaml's workspace, every .proto at once.
BUF = $(call toolchain_bin,buf)

# What CI's Lint steps run clang-tidy's full profile on: every src .cpp and the
# C++ tools' (tools/agent), built with them, except the audio module's silent
# output, which CI's Linux build compiles without the client: either platform's
# default build has the client, so compiles the output device instead. On
# Windows, the client's and the Renderer's tests too, as CI's client job lints
# them; elsewhere, not the client's trees, the window module's GLFW half or the
# output device, which CI's Linux build (without the client, AUGUSTA_CLIENT) has
# no compile commands for. Never another platform's half of a module
# (<module>_windows.cpp, <module>_linux.cpp, <module>_macos.cpp, and
# <module>_posix.cpp, Linux's and macOS's), which this build doesn't compile.
TIDY_EXCLUDES := ":(exclude)src/modules/audio/output_none.cpp"
ifeq ($(OS),Windows_NT)
TIDY_CLIENT_TESTS := "tests/client_*.cpp" "tests/renderer_*.cpp"
OTHER_PLATFORMS := ":(exclude)src/*_linux.cpp" ":(exclude)src/*_macos.cpp" ":(exclude)src/*_posix.cpp"
TIDY_EXCLUDES += $(OTHER_PLATFORMS)
else
ifeq ($(HOST_OS),Darwin)
OTHER_PLATFORMS := ":(exclude)src/*_linux.cpp"
else
OTHER_PLATFORMS := ":(exclude)src/*_macos.cpp"
endif
TIDY_EXCLUDES += ":(exclude)src/*_windows.cpp" $(OTHER_PLATFORMS)
TIDY_EXCLUDES += ":(exclude)src/client/*" ":(exclude)src/modules/renderer/*"
TIDY_EXCLUDES += ":(exclude)src/modules/audio/output_miniaudio.cpp" ":(exclude)src/modules/audio/miniaudio.cpp"
TIDY_EXCLUDES += ":(exclude)src/modules/window/window.cpp" ":(exclude)src/modules/window/glfw_keys.cpp"
endif
# Tracked or new and not ignored, as `make prepush` hands over files not added
# yet: a new file a hook passes is linted, not dropped.
LINT_LS_FILES := git ls-files --cached --others --exclude-standard
TIDY_SOURCES := $(shell $(LINT_LS_FILES) -- "src/*.cpp" "tools/agent/*.cpp" $(TIDY_CLIENT_TESTS) ":(exclude)tools/*/tests/*" $(TIDY_EXCLUDES))

# The rest of src and tests that this platform's build has compile commands
# for, which tidy holds to include-cleaner alone (.clang-tidy's
# misc-include-cleaner): the tests, except the client's and the Renderer's on
# Windows.
# -w because the presets compile with -Werror (/WX), which turns warnings in
# third-party headers into errors that cut the parse short and leave includes
# looking unused.
ifeq ($(OS),Windows_NT)
INCLUDE_EXCLUDES := ":(exclude)src/modules/audio/output_none.cpp" $(OTHER_PLATFORMS)
JOBS ?= $(NUMBER_OF_PROCESSORS)
else
INCLUDE_EXCLUDES := $(TIDY_EXCLUDES) ":(exclude)tests/client_*"
JOBS ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu)
endif
# Nor the allocator benchmarks' own files, anywhere: only a configure that asks
# for them (TAGS="allocator") compiles them, which CI's never does.
INCLUDE_EXCLUDES += ":(exclude)tests/allocator_*"
INCLUDE_SOURCES := $(filter-out $(TIDY_SOURCES),$(shell $(LINT_LS_FILES) -- "src/*.cpp" "tests/*.cpp" "tools/agent/tests/*.cpp" $(INCLUDE_EXCLUDES)))
# One target per file, so a parallel make runs them side by side.
TIDY_CHECKS := $(addprefix tidy/,$(TIDY_SOURCES))
INCLUDE_CHECKS := $(addprefix include-cleaner/,$(INCLUDE_SOURCES))

.DEFAULT_GOAL := all
.PHONY: all help configure build test labels check coverage profile tools install uninstall clean distclean format format-check tidy tidy-prepare lint prepush docs toolchain toolchain-check

all: build

# $(info), not echo: make prints it itself, so the parentheses reach neither
# cmd.exe nor /bin/sh, which would read them as syntax.
help:
	$(info Usage: make [target] [PRESET=...] [FILES="a b ..."] [TAGS="a b ..."])
	$(info $()  PRESET is $(PRESET) unless set, one of this host's presets listed below:)
	$(info $()  the shipping ones make a Shipping build, without the debug tools,)
	$(info $()  every other one a Development build, with them (ADR-0074).)
	$(info )
	$(info Build:)
	$(info $()  all              the default: build)
	$(info $()  configure        cmake --preset)
	$(info $()  build            configure, then compile the binaries (no tests), or TARGETS=augustad ... alone)
	$(info $()  tools            the tools alone, the Agents' and the asset cooker's modules among them (PYTHON_EXECUTABLE=...))
	$(info $()  docs             the documentation site, MkDocs and Doxygen, into build/docs-site)
	$(info )
	$(info Test:)
	$(info $()  test             build, then compile the tests and run ctest, or TAGS="property ..." the tests with any of those labels alone:)
	$(info $()                   TAGS=<area> ... (animation, ...) compiles that area's test executables alone)
	$(info $()  labels           configure, then list the test areas, and the labels ctest knows (the GoogleTest ones once test has built them, allocator with TAGS="allocator"))
	$(info $()  check            configure, then the repository's structural checks (checks/) and their pytest)
	$(info $()  coverage         test under the linux-coverage preset, then its report into build/x64-linux-coverage/report)
	$(info $()  profile          build augustac, then run it under nsys, which records the Profiling panel's capture into $(PROFILE_DIR))
	$(info )
	$(info Format and lint, on every tracked file, or FILES alone as the hooks run them:)
	$(info $()  format           every formatter (C++, YAML, Lua, TOML, Python, shell, CMake, Markdown, protobuf))
	$(info $()  format-check     every formatter check and linter CI's format job runs (not clang-tidy))
	$(info $()  tidy             clang-tidy on src, include-cleaner on what it leaves out, as CI runs them)
	$(info $()  lint             format-check and tidy: everything CI lints)
	$(info $()  prepush          lint on this branch's unpushed commits and uncommitted changes, as the pre-push hook does)
	$(info )
	$(info Install and clean:)
	$(info $()  install          build, then cmake --install augustad (prefix=..., DESTDIR=...))
	$(info $()  uninstall        remove what install put in place (same DESTDIR))
	$(info $()  clean            remove build outputs, keep the configuration)
	$(info $()  distclean        delete $(BUILD_DIR))
	$(info )
	$(info Toolchain:)
	$(info $()  toolchain        install the pinned tools missing from the shared store, then link .toolchain/bin to them)
	$(info $()  toolchain-check  fail on each .toolchain/bin tool that isn't its pinned version, and each editor path to none)
	$(info )
	@cmake --list-presets

# TAGS naming allocator asks the configure for the allocator benchmarks
# (tests/CMakeLists.txt), and for that configure alone: the next one without it
# has none of them again.
configure:
	$(RUN) cmake --preset $(PRESET) $(if $(filter allocator,$(TAGS)),-DAUGUSTA_ALLOCATOR_BENCHMARKS=ON)

# TARGETS narrows the build to those CMake targets, as the VS Code launch
# configs' tasks (.vscode/tasks.json) build only the executable they debug.
build: configure
	$(RUN) cmake --build --preset $(PRESET) $(if $(TARGETS),--target $(TARGETS))

# TAGS="a b ..." narrows the run to the tests with any of those ctest labels,
# as one anchored regex; a label no test has fails the run rather than passing
# with nothing run.
space := $() $()
TAGS_REGEX = "^($(subst $(space),|,$(strip $(TAGS))))$$"
# The labels every test preset leaves out of a run (netcode, allocator:
# CMakePresets.json) give way to TAGS, which names them, and to labels, which
# lists them: an exclusion that matches no label replaces the preset's.
NO_EXCLUSION := --label-exclude "^$$"

# The test areas this build's executables carry, as tests/CMakeLists.txt
# writes them when it configures: read when test or labels runs, after its
# configure.
TEST_AREAS = $(file <$(BUILD_DIR)/tests/test_areas.txt)
# augusta_tests_<area> for each of TAGS when every tag is an area, so only
# those areas' executables build; augusta_tests, every test, otherwise.
TEST_TARGETS = $(if $(and $(strip $(TAGS)),$(if $(filter-out $(TEST_AREAS),$(TAGS)),,all)),$(addprefix augusta_tests_,$(TAGS)),augusta_tests)

# The tests are not part of build (ADR-0008): augusta_tests builds them, or
# TAGS=<area> that area's alone.
test: build
	$(RUN) cmake --build --preset $(PRESET) --target $(TEST_TARGETS)
	$(RUN) ctest --preset $(PRESET) --parallel $(JOBS) $(if $(strip $(TAGS)),--label-regex $(TAGS_REGEX) $(NO_EXCLUSION) --no-tests=error)

# The labels TAGS picks from: first the test areas, read from what configure
# wrote, so they show before any test is built; then the labels ctest knows in
# this build. gtest_discover_tests labels its tests when they are built, so
# ctest lists the areas, property and netcode once test has run; allocator, in
# a build configured with TAGS="allocator".
labels: configure
	$(info Test areas, each of which TAGS=<area> builds alone:)
	$(foreach area,$(sort $(TEST_AREAS)),$(info $()  $(area)))
	$(RUN) ctest --preset $(PRESET) --print-labels $(NO_EXCLUSION)

# The structural checks (checks/CMakeLists.txt) build nothing: configuring
# registers them, and ctest's check label picks them alone. Then the pytest of
# the scripts they cover, as CI runs it.
check: configure
	$(RUN) ctest --preset $(PRESET) --parallel $(JOBS) --label-regex check --no-tests=error
	$(RUN) uv run --no-project --with pyyaml==6.0.3 --with pytest==9.1.1 pytest checks

# The tests' coverage (ADR-0013), whatever PRESET says: the linux-coverage
# preset's tests, then their llvm-cov report. The last run's profiles go first,
# so the report is of this run alone.
coverage:
	cmake -E rm -rf build/x64-linux-coverage/profiles
	$(MAKE) --no-print-directory test PRESET=linux-coverage
	bash scripts/coverage-report.sh

# A Development client under Nsight Systems, which records alone the window of
# frames its Profiling Debug panel marks, with the flags docs/agents/tooling.md
# explains. nsys stops the client once the window closes and writes
# augustac-<pid>.nsys-rep into PROFILE_DIR (not profiles/, which coverage
# empties). Nsight Systems profiles no macOS process. NSYS names the nsys to
# run: on Windows, which leaves it off PATH, the newest Nsight Systems under
# Program Files by default (ProgramW6432: a 32-bit make's ProgramFiles is
# Program Files (x86)). Recursive, so only profile looks for it. %p reaches nsys,
# which puts the PID in its place: cmd.exe expands only a %name% pair, and the
# line holds no other %.
PROFILE_DIR ?= $(BUILD_DIR)/nsys
# $(1) quoted, or an error naming NSYS when it is empty.
nsys_found = $(if $(1),"$(1)",$(error Nsight Systems not found under Program Files: install it, or set NSYS to its nsys))
ifeq ($(OS),Windows_NT)
NSYS ?= $(call nsys_found,$(shell powershell -NoProfile -Command "Get-Item (Join-Path $$env:ProgramW6432 'NVIDIA Corporation\Nsight Systems *\target-windows-x64\nsys.exe') -ErrorAction SilentlyContinue | Sort-Object { [version]($$_.Directory.Parent.Name -replace 'Nsight Systems ') } | Select-Object -Last 1 -ExpandProperty FullName"))
else
NSYS ?= nsys
endif
profile:
	$(if $(filter Darwin,$(HOST_OS)),$(error Nsight Systems profiles no macOS process: run make profile on Windows or Linux),@:)
	$(if $(filter %shipping,$(PRESET)),$(error A Shipping client has no Profiling panel (ADR-0074): pick a Development preset, e.g. PRESET=$(subst -shipping,,$(PRESET))),@:)
	$(MAKE) --no-print-directory build PRESET=$(PRESET) TARGETS=augustac
	cmake -E make_directory $(PROFILE_DIR)
	$(NSYS) profile --capture-range=nvtx --nvtx-capture=augusta.capture --env-var=NSYS_NVTX_PROFILER_REGISTER_ONLY=0 "--output=$(abspath $(PROFILE_DIR))/augustac-%p" "$(BUILD_DIR)/src/client/augustac" --config config/augustac.yaml

# The tools alone (tools/, augusta_tools), as the asset cooker's bootstrap
# builds them (tools/pack/scripts/bootstrap.sh): the preset's build without the
# client, in a build directory of its own, so the preset's own keeps its
# configuration. PYTHON_EXECUTABLE names the Python 3.12 their modules are built
# against, the tools environment's by default (cmake/AugustaToolsPython.cmake).
TOOLS_BUILD_DIR := $(BUILD_DIR)-tools
tools:
	$(RUN) cmake --preset $(PRESET) -B $(TOOLS_BUILD_DIR) -DAUGUSTA_CLIENT=OFF $(if $(PYTHON_EXECUTABLE),"-DPYTHON_EXECUTABLE=$(PYTHON_EXECUTABLE)")
	$(RUN) cmake --build $(TOOLS_BUILD_DIR) --target augusta_tools

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

# format, format-check, tidy and lint run on every tracked file, or on FILES="a
# b ..." alone, the files the hooks hand over: each formatter and linter on the
# ones it covers, so a hook never states their commands again. One that covers
# none of them doesn't run.
#
# The files of FILES that the pathspecs $(1) cover, tracked or new and not
# ignored, as `make prepush` hands over the files not added yet.
covered = $(filter $(FILES),$(shell git ls-files --cached --others --exclude-standard -- $(1)))
# Those, or every tracked file the pathspecs $(1) cover without FILES.
sources = $(if $(FILES),$(call covered,$(1)),$(shell git ls-files -- $(1)))
# The same, or all of them when FILES holds one of $(2), the linter's own
# configuration, which reaches every file it lints.
linted = $(if $(filter $(2),$(FILES)),$(shell git ls-files -- $(1)),$(call sources,$(1)))

format:
	$(if $(call sources,$(CXX_SOURCES)),$(CLANG_FORMAT) -i $(call sources,$(CXX_SOURCES)),@:)
	$(if $(call sources,$(YAML_SOURCES)),$(YAMLFMT) $(call sources,$(YAML_SOURCES)),@:)
	$(if $(call sources,$(LUA_SOURCES)),$(STYLUA) $(call sources,$(LUA_SOURCES)),@:)
	$(if $(call sources,$(TOML_SOURCES)),$(TAPLO) fmt $(call sources,$(TOML_SOURCES)),@:)
	$(if $(call sources,$(PYTHON_SOURCES)),$(RUFF) format --quiet $(call sources,$(PYTHON_SOURCES)),@:)
	$(if $(call sources,$(SHELL_SOURCES)),$(SHFMT) -w $(call sources,$(SHELL_SOURCES)),@:)
	$(if $(call sources,$(CMAKE_SOURCES)),$(GERSEMI) --quiet -i $(call sources,$(CMAKE_SOURCES)),@:)
	$(if $(call sources,$(MARKDOWN_SOURCES)),$(PRETTIER) --log-level warn --write $(call sources,$(MARKDOWN_SOURCES)),@:)
	$(if $(call sources,$(PROTO_SOURCES)),$(BUF) format -w,@:)

# One target per formatter and linter, in this order, so CI's format job runs
# each of them alone with the command written here.
FORMAT_CHECKS := $(addprefix format-check/,clang-format yamlfmt yamllint stylua luacheck taplo ruff shfmt shellcheck actionlint gersemi prettier pymarkdown buf path-text)
.PHONY: $(FORMAT_CHECKS)
format-check: $(FORMAT_CHECKS)
format-check/clang-format:
	$(if $(call sources,$(CXX_SOURCES)),$(CLANG_FORMAT) --dry-run --Werror $(call sources,$(CXX_SOURCES)),@:)
format-check/yamlfmt:
	$(if $(call sources,$(YAML_SOURCES)),$(YAMLFMT) -lint $(call sources,$(YAML_SOURCES)),@:)
format-check/yamllint:
	$(if $(call linted,$(YAML_SOURCES),.yamllint .yamlignore .gitignore),$(YAMLLINT) $(call linted,$(YAML_SOURCES),.yamllint .yamlignore .gitignore),@:)
format-check/stylua:
	$(if $(call sources,$(LUA_SOURCES)),$(STYLUA) --check $(call sources,$(LUA_SOURCES)),@:)
format-check/luacheck:
	$(if $(call linted,$(LUA_SOURCES),.luacheckrc),$(LUACHECK) $(call linted,$(LUA_SOURCES),.luacheckrc),@:)
format-check/taplo:
	$(if $(call sources,$(TOML_SOURCES)),$(TAPLO) fmt --check $(call sources,$(TOML_SOURCES)),@:)
	$(if $(call sources,$(TOML_SOURCES)),$(TAPLO) lint $(call sources,$(TOML_SOURCES)),@:)
format-check/ruff:
	$(if $(call sources,$(PYTHON_SOURCES)),$(RUFF) format --check $(call sources,$(PYTHON_SOURCES)),@:)
	$(if $(call linted,$(PYTHON_SOURCES),ruff.toml),$(RUFF) check $(call linted,$(PYTHON_SOURCES),ruff.toml),@:)
format-check/shfmt:
	$(if $(call sources,$(SHELL_SOURCES)),$(SHFMT) -d $(call sources,$(SHELL_SOURCES)),@:)
format-check/shellcheck:
	$(if $(call linted,$(SHELL_SOURCES),.shellcheckrc),$(SHELLCHECK) $(call linted,$(SHELL_SOURCES),.shellcheckrc),@:)
# actionlint reads the workflows together: a call into another is checked
# against it.
format-check/actionlint:
	$(if $(call linted,".github/workflows/*",.github/actionlint.yaml),$(ACTIONLINT),@:)
format-check/gersemi:
	$(if $(call sources,$(CMAKE_SOURCES)),$(GERSEMI) --check $(call sources,$(CMAKE_SOURCES)),@:)
format-check/prettier:
	$(if $(call sources,$(MARKDOWN_SOURCES)),$(PRETTIER) --log-level warn --check $(call sources,$(MARKDOWN_SOURCES)),@:)
format-check/pymarkdown:
	$(if $(call linted,$(MARKDOWN_SOURCES),.pymarkdown.json),$(PYMARKDOWN) scan $(call linted,$(MARKDOWN_SOURCES),.pymarkdown.json),@:)
# buf reads the workspace whole, as buf.yaml names it.
format-check/buf:
	$(if $(call linted,$(PROTO_SOURCES),buf.yaml),$(BUF) format -d --exit-code,@:)
	$(if $(call linted,$(PROTO_SOURCES),buf.yaml),$(BUF) lint,@:)
# No path::string() or generic_string() in src (checks/path_text.cmake): FILES'
# sources, or every one, which the script lists itself so no command line holds
# them all, without FILES or when it holds the script.
PATH_TEXT_SOURCES := "src/*.cpp" "src/*.h"
format-check/path-text:
	$(if $(call linted,$(PATH_TEXT_SOURCES),checks/path_text.cmake),cmake -DSOURCE_DIR=. -P checks/path_text.cmake $(if $(filter checks/path_text.cmake,$(FILES)),,$(if $(FILES),$(call covered,$(PATH_TEXT_SOURCES)))),@:)

# tidy's per-file checks: every one, or FILES' alone.
TIDY_RUN := $(if $(FILES),$(strip $(filter $(addprefix tidy/,$(FILES)),$(TIDY_CHECKS)) $(filter $(addprefix include-cleaner/,$(FILES)),$(INCLUDE_CHECKS))),$(TIDY_CHECKS) $(INCLUDE_CHECKS))
# The per-file targets side by side, each reporting what it finds.
RUN_CHECKS = $(MAKE) --no-print-directory -j $(JOBS) -k -Otarget "FILES=$(FILES)" $(if $(TIDY_RUN),"CTCACHE_DIR=$(CTCACHE_DIR)")

# Configuring first keeps compile_commands.json in step with the sources (a
# file new on a branch has no entry until then), except with FILES, which
# would make every push wait on CMake and vcpkg: that build must have been
# configured. Then protoc writes augusta.pb.h, which the protocol module's
# sources include.
tidy-prepare: $(if $(FILES),,configure)
	$(if $(TIDY_RUN),$(if $(wildcard $(BUILD_DIR)/compile_commands.json),,$(error $(BUILD_DIR)/compile_commands.json not found: configure a build first (make configure PRESET=$(PRESET)))))
	$(if $(filter tidy/src/modules/protocol/%,$(TIDY_RUN)),$(RUN) cmake --build --preset $(PRESET) --target augusta_protocol_generated,@:)

tidy: tidy-prepare
	$(if $(TIDY_RUN),$(RUN_CHECKS) $(TIDY_RUN),@:)

lint: tidy-prepare
	$(RUN_CHECKS) $(FORMAT_CHECKS) $(TIDY_RUN)

# The toolchain's clang-tidy through its ctcache, as CI's Lint steps run it: a
# file whose preprocessed source, configuration and arguments passed before is
# skipped. On macOS, Homebrew's clang takes its SDK from a configuration file
# that clang-tidy does not read: the SDK that the clang beside the linked
# clang-tidy resolves, handed to clang-tidy.
ifeq ($(HOST_OS),Darwin)
TIDY_SYSROOT := --extra-arg=-isysroot$(shell $(dir $(realpath .toolchain/bin/clang-tidy))clang++ -\#\#\# -x c++ -c /dev/null 2>&1 | sed -n 's/.*"-isysroot" "\([^"]*\)".*/\1/p')
endif
CLANG_TIDY = $(call toolchain_bin,clang-tidy-cache) $(call toolchain_bin,clang-tidy) $(TIDY_SYSROOT)
# ctcache's results live under build/, for every preset, apart per clang-tidy
# version (the first number the toolchain's `clang-tidy --version` prints),
# which ctcache's key leaves out. Recursive, so only tidy and lint run
# clang-tidy for it, and hand it to the per-file targets.
CTCACHE_DIR = $(abspath build/ctcache)/$(firstword $(filter 1% 2% 3% 4% 5% 6% 7% 8% 9%,$(shell $(call toolchain_bin,clang-tidy) --version)))

.PHONY: $(TIDY_CHECKS) $(INCLUDE_CHECKS)
$(TIDY_CHECKS): tidy/%:
	$(RUN) $(CLANG_TIDY) -p=$(BUILD_DIR) $*

$(INCLUDE_CHECKS): include-cleaner/%:
	$(RUN) $(CLANG_TIDY) --quiet -p=$(BUILD_DIR) --checks=-*,misc-include-cleaner --extra-arg=-w $*

# The toolchain (ADR-0077), by scripts/bootstrap/toolchain.sh, which says how:
# toolchain installs what versions.env pins and links .toolchain/bin to it;
# toolchain-check reads it and fails on each tool it finds stale, as the
# pre-commit hook runs it. Git's bash on Windows, which runs the hooks too.
toolchain:
	bash scripts/bootstrap/toolchain.sh install

toolchain-check:
	bash scripts/bootstrap/toolchain.sh check

# The pre-push hook's checks, as a push of HEAD now would run them: on the
# files of its commits the remote lacks, and of the changes not committed yet,
# Git LFS left out. git runs the hook,
# so it gets git's own shell on every platform. The hook reads only the
# pushed commit from its stdin line, so HEAD stands in for both refs.
PREPUSH_REFS := $(shell git rev-parse --git-path prepush-refs)
prepush: export PRE_PUSH_CHECKS_ONLY = 1
prepush: export PRE_PUSH_WORKTREE = 1
prepush:
	$(file >$(PREPUSH_REFS),HEAD $(shell git rev-parse HEAD) HEAD 0000000000000000000000000000000000000000)
	git hook run --to-stdin=$(PREPUSH_REFS) pre-push -- origin
