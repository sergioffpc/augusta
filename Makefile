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

ifeq ($(OS),Windows_NT)
SHELL := cmd.exe
.SHELLFLAGS := /c
PRESET ?= windows
RUN := scripts\vcenv.cmd
else ifeq ($(shell uname -s),Darwin)
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
# What .yamlfmt and .yamllint cover, named file by file for format-files and
# lint-files.
YAML_SOURCES := "*.yaml" "*.yml" "*.yamlfmt" "*.yamllint" ":!:third_party/*" \
  ":!:charts/augustad/templates/*" ":!:clusters/onprem/flux-system/gotk-components.yaml" \
  ":!:clusters/onprem/infrastructure/kube-bench/cfg/*"

# The formatters and linters uv runs, at the versions CI pins.
RUFF := uv tool run ruff@0.16.10
SHFMT := uv tool run --from shfmt-py==4.2.0 shfmt
SHELLCHECK := uv tool run --from shellcheck-py==0.11.0.1 shellcheck
ACTIONLINT := uv tool run --from actionlint-py==1.7.12.25 --with shellcheck-py==0.11.0.1 actionlint
GERSEMI := uv tool run gersemi@0.29.2
PYMARKDOWN := uv tool run --from pymarkdownlnt==0.9.40 pymarkdown --config .pymarkdown.json
YAMLLINT := uv tool run --from yamllint==1.37.1 yamllint --strict -c .yamllint
# Prettier is a Node package: uv runs Node from its PyPI wheel, and npx Prettier.
PRETTIER := uv tool run --from nodejs-wheel==24.19.0 npx --yes prettier@3.9.9
# Buf too, from npm: it reads buf.yaml's workspace, every .proto at once.
BUF := uv tool run --from nodejs-wheel==24.19.0 npx --yes @bufbuild/buf@1.73.0

# What CI's Lint steps run clang-tidy's full profile on: every src .cpp and the
# C++ tools' (tools/agent), built with them, except the audio module's silent
# output, which CI's Linux build compiles without the client: either platform's
# default build has the client, so compiles the output device instead. On
# Windows, the client's and the Renderer's tests too, as CI's client job lints
# them; elsewhere, not the client's trees, the window module's GLFW half or the
# output device, which CI's Linux build (without the client, AUGUSTA_CLIENT) has
# no compile commands for.
TIDY_EXCLUDES := ":(exclude)src/modules/audio/output_none.cpp"
ifeq ($(OS),Windows_NT)
TIDY_CLIENT_TESTS := "tests/client_*.cpp" "tests/renderer_*.cpp"
else
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
INCLUDE_EXCLUDES := ":(exclude)src/modules/audio/output_none.cpp"
JOBS ?= $(NUMBER_OF_PROCESSORS)
else
INCLUDE_EXCLUDES := $(TIDY_EXCLUDES) ":(exclude)tests/client_*"
JOBS ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu)
endif
INCLUDE_SOURCES := $(filter-out $(TIDY_SOURCES),$(shell $(LINT_LS_FILES) -- "src/*.cpp" "tests/*.cpp" "tools/agent/tests/*.cpp" $(INCLUDE_EXCLUDES)))
# One target per file, so a parallel make runs them side by side.
TIDY_CHECKS := $(addprefix tidy/,$(TIDY_SOURCES))
INCLUDE_CHECKS := $(addprefix include-cleaner/,$(INCLUDE_SOURCES))

.DEFAULT_GOAL := all
.PHONY: all help configure build test check coverage pack-native install uninstall clean distclean format format-check format-files tidy lint lint-files prepush docs

all: build

# $(info), not echo: make prints it itself, so the parentheses reach neither
# cmd.exe nor /bin/sh, which would read them as syntax.
help:
	$(info Presets: windows-shipping and linux-shipping make a Shipping build, without the debug tools;)
	$(info $()  every other preset a Development build, with them (ADR-0074).)
	$(info Targets (PRESET=$(PRESET), override with PRESET=windows-debug, linux-san, ...):)
	$(info $()  all           the default: build)
	$(info $()  configure     cmake --preset)
	$(info $()  build         configure, then compile the binaries (no tests), or TARGETS=augustad ... alone)
	$(info $()  test          build, then compile the tests and run ctest)
	$(info $()  check         configure, then the repository's structural checks (checks/) and their pytest)
	$(info $()  coverage      test under the linux-coverage preset, then its report into build/x64-linux-coverage/report)
	$(info $()  pack-native   the asset cooker's native module alone, into tools/pack (PYTHON_EXECUTABLE=...))
	$(info $()  install       build, then cmake --install augustad (prefix=..., DESTDIR=...))
	$(info $()  uninstall     remove what install put in place (same DESTDIR))
	$(info $()  clean         remove build outputs, keep the configuration)
	$(info $()  distclean     delete $(BUILD_DIR))
	$(info $()  format        every formatter (C++, YAML, Lua, TOML, Python, shell, CMake, Markdown) on tracked files)
	$(info $()  format-files  format's formatters on FILES alone, as the pre-commit hook runs them)
	$(info $()  format-check  every formatter check and linter CI's format job runs (not clang-tidy))
	$(info $()  tidy          clang-tidy on src, include-cleaner on what it leaves out, as CI runs them (configures first))
	$(info $()  lint          format-check, then tidy: everything CI lints)
	$(info $()  lint-files    lint's linters and tidy's clang-tidy on FILES alone, as the pre-push hook runs them)
	$(info $()  prepush       the pre-push hook's checks on this branch's unpushed commits and uncommitted changes, without pushing)
	$(info $()  docs          the documentation site, MkDocs and Doxygen, into build/docs-site)
	@:

configure:
	$(RUN) cmake --preset $(PRESET)

# TARGETS narrows the build to those CMake targets, as the VS Code launch
# configs' tasks (.vscode/tasks.json) build only the executable they debug.
build: configure
	$(RUN) cmake --build --preset $(PRESET) $(if $(TARGETS),--target $(TARGETS))

# The tests are not part of build (ADR-0008): augusta_tests builds them.
test: build
	$(RUN) cmake --build --preset $(PRESET) --target augusta_tests
	$(RUN) ctest --preset $(PRESET) --parallel $(JOBS)

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

# The asset cooker's native module alone (tools/pack/native, ADR-0030), which
# its bootstrap builds (tools/pack/scripts/bootstrap.sh): the preset's build
# without the client, in a build directory of its own, so the preset's own
# keeps its configuration. PYTHON_EXECUTABLE names the Python 3.12 it is built
# against, the tools environment's by default (cmake/AugustaToolsPython.cmake).
PACK_BUILD_DIR := $(BUILD_DIR)-pack
pack-native:
	$(RUN) cmake --preset $(PRESET) -B $(PACK_BUILD_DIR) -DAUGUSTA_CLIENT=OFF $(if $(PYTHON_EXECUTABLE),"-DPYTHON_EXECUTABLE=$(PYTHON_EXECUTABLE)")
	$(RUN) cmake --build $(PACK_BUILD_DIR) --target augusta_pack_py

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
	$(BUF) format -w

format-check:
	clang-format --dry-run --Werror $(shell git ls-files -- $(CXX_SOURCES))
	yamlfmt -conf .yamlfmt -lint
	$(YAMLLINT) .
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
	$(BUF) format -d --exit-code
	$(BUF) lint

# Configuring first keeps
# compile_commands.json in step with the sources (a file new on a branch has no
# entry until then); then protoc writes augusta.pb.h, which protocol.cpp
# includes.
tidy: configure
	$(RUN) cmake --build --preset $(PRESET) --target augusta_protocol_generated
	$(MAKE) --no-print-directory -j $(JOBS) -k -Otarget "CTCACHE_DIR=$(CTCACHE_DIR)" $(TIDY_CHECKS) $(INCLUDE_CHECKS)

# clang-tidy through ctcache (scripts/bootstrap.sh installs it), as CI's Lint
# steps run it: a file whose preprocessed source, configuration and arguments
# passed before is skipped. Its results live under build/, for every preset,
# apart per clang-tidy version (the first number `clang-tidy --version`
# prints), which ctcache's key leaves out. Recursive, so only tidy and lint-files
# run clang-tidy for it, and hand it to the per-file targets.
CLANG_TIDY := clang-tidy-cache clang-tidy
# On macOS, Homebrew's clang takes its SDK from a configuration file that
# clang-tidy does not read: the SDK that clang resolves, handed to clang-tidy.
ifeq ($(shell uname -s),Darwin)
CLANG_TIDY += --extra-arg=-isysroot$(shell $(or $(HOMEBREW_PREFIX),/opt/homebrew)/opt/llvm@21/bin/clang++ -\#\#\# -x c++ -c /dev/null 2>&1 | sed -n 's/.*"-isysroot" "\([^"]*\)".*/\1/p')
endif
CTCACHE_DIR = $(abspath build/ctcache)/$(firstword $(filter 1% 2% 3% 4% 5% 6% 7% 8% 9%,$(shell clang-tidy --version)))

.PHONY: $(TIDY_CHECKS) $(INCLUDE_CHECKS)
$(TIDY_CHECKS): tidy/%:
	$(RUN) $(CLANG_TIDY) -p=$(BUILD_DIR) $*

$(INCLUDE_CHECKS): include-cleaner/%:
	$(RUN) $(CLANG_TIDY) --quiet -p=$(BUILD_DIR) --checks=-*,misc-include-cleaner --extra-arg=-w $*

lint: format-check tidy

# The hooks' half of format and lint, on the files they hand over (FILES="a b
# ..."): each formatter and linter runs on the ones it covers, with the commands
# above, so a hook never states them again.
#
# The files of FILES that the pathspecs $(1) cover, tracked or new and not
# ignored, as `make prepush` hands over the files not added yet.
covered = $(filter $(FILES),$(shell git ls-files --cached --others --exclude-standard -- $(1)))
# The same, or all of them when FILES holds one of $(2), the linter's own
# configuration, which reaches every file it lints.
linted = $(if $(filter $(2),$(FILES)),$(shell git ls-files -- $(1)),$(call covered,$(1)))

format-files:
	$(if $(call covered,$(CXX_SOURCES)),clang-format -i $(call covered,$(CXX_SOURCES)),@:)
	$(if $(call covered,$(YAML_SOURCES)),yamlfmt -conf .yamlfmt $(call covered,$(YAML_SOURCES)),@:)
	$(if $(call covered,$(LUA_SOURCES)),stylua $(call covered,$(LUA_SOURCES)),@:)
	$(if $(call covered,$(TOML_SOURCES)),taplo fmt $(call covered,$(TOML_SOURCES)),@:)
	$(if $(call covered,$(PYTHON_SOURCES)),$(RUFF) format --quiet $(call covered,$(PYTHON_SOURCES)),@:)
	$(if $(call covered,$(SHELL_SOURCES)),$(SHFMT) -w $(call covered,$(SHELL_SOURCES)),@:)
	$(if $(call covered,$(CMAKE_SOURCES)),$(GERSEMI) --quiet -i $(call covered,$(CMAKE_SOURCES)),@:)
	$(if $(call covered,$(MARKDOWN_SOURCES)),$(PRETTIER) --log-level warn --write $(call covered,$(MARKDOWN_SOURCES)),@:)
	$(if $(call covered,$(PROTO_SOURCES)),$(BUF) format -w,@:)

# format-check's linters, then tidy's clang-tidy and include-cleaner, side by
# side as tidy runs them. Unlike tidy it does not configure first, which would
# make every push wait on CMake and vcpkg: the build must have been configured.
# It generates augusta.pb.h when it lints protocol.cpp, as tidy does.
LINT_CHECKS := $(addprefix lint/,yamllint luacheck taplo ruff shellcheck pymarkdown actionlint buf)
LINT_TIDY := $(strip $(filter $(addprefix tidy/,$(FILES)),$(TIDY_CHECKS)) $(filter $(addprefix include-cleaner/,$(FILES)),$(INCLUDE_CHECKS)))
lint-files:
	$(if $(LINT_TIDY),$(if $(wildcard $(BUILD_DIR)/compile_commands.json),,$(error $(BUILD_DIR)/compile_commands.json not found: configure a build first (make configure PRESET=$(PRESET)))))
	$(if $(filter tidy/src/modules/protocol/protocol.cpp,$(LINT_TIDY)),$(RUN) cmake --build --preset $(PRESET) --target augusta_protocol_generated,@:)
	$(MAKE) --no-print-directory -j $(JOBS) -k -Otarget "FILES=$(FILES)" $(if $(strip $(LINT_TIDY)),"CTCACHE_DIR=$(CTCACHE_DIR)") $(LINT_CHECKS) $(LINT_TIDY)

.PHONY: $(LINT_CHECKS)
lint/yamllint:
	$(if $(call linted,$(YAML_SOURCES),.yamllint .yamlignore .gitignore),$(YAMLLINT) $(call linted,$(YAML_SOURCES),.yamllint .yamlignore .gitignore),@:)
lint/luacheck:
	$(if $(call linted,$(LUA_SOURCES),.luacheckrc),luacheck $(call linted,$(LUA_SOURCES),.luacheckrc),@:)
lint/taplo:
	$(if $(call covered,$(TOML_SOURCES)),taplo lint $(call covered,$(TOML_SOURCES)),@:)
lint/ruff:
	$(if $(call linted,$(PYTHON_SOURCES),ruff.toml),$(RUFF) check $(call linted,$(PYTHON_SOURCES),ruff.toml),@:)
lint/shellcheck:
	$(if $(call linted,$(SHELL_SOURCES),.shellcheckrc),$(SHELLCHECK) $(call linted,$(SHELL_SOURCES),.shellcheckrc),@:)
lint/pymarkdown:
	$(if $(call linted,$(MARKDOWN_SOURCES),.pymarkdown.json),$(PYMARKDOWN) scan $(call linted,$(MARKDOWN_SOURCES),.pymarkdown.json),@:)
# actionlint reads the workflows together: a call into another is checked
# against it.
lint/actionlint:
	$(if $(filter .github/workflows/% .github/actionlint.yaml,$(FILES)),$(ACTIONLINT),@:)
# buf lint reads the workspace whole, as buf.yaml names it.
lint/buf:
	$(if $(or $(call covered,$(PROTO_SOURCES)),$(filter buf.yaml,$(FILES))),$(BUF) lint,@:)

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
