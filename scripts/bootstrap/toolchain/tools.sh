# shellcheck shell=bash
# shellcheck disable=SC2034 # its globals are the other parts'
# The toolchain's tools, for scripts/bootstrap/toolchain.sh, which sources
# this: which there are, and how each is installed and tells its version.

# The toolchain's tools, by the names the store files them under.
readonly TOOLS=(ruff shfmt shellcheck actionlint gersemi pymarkdown yamllint
  ctcache node yamlfmt stylua luacheck taplo buf trivy kubeconform kyverno
  promtool amtool hadolint oras syft grype yq flux cosign)

# LLVM's tools, which .toolchain/bin links to the system's LLVM: a package
# manager installs it, pinned to its major (ADR-0008), not the store.
readonly LLVM_TOOLS=(clangd clang-format clang-tidy)

# Describes a tool, without a command substitution, as each forks, slowly on
# Windows.
# Globals:
#   version_name: the versions.env variable that pins it.
#   version: that variable's value.
#   source: where it comes from: uv, which installs package from PyPI, or
#     release, its platform's release download (toolchain_release in
#     scripts/bootstrap/<platform>.sh).
#   package: the PyPI requirement uv installs.
#   rock: the LuaRocks rock that builds it where its release has no build for
#     the platform, or nothing.
#   command: the command it puts in .toolchain/bin.
#   without_exe: set when Windows' .toolchain/bin also needs that command
#     without .exe.
#   probe: the arguments that make it print its version.
#   expected: the start of the line of that output the pinned version prints,
#     up to a space or the line's end, or nothing when its output names no
#     version the pin gives. A wheel that packages another project's binary
#     (shellcheck-py, actionlint-py) adds a fourth number of its own to the
#     binary's version, which the binary does not report.
# Arguments:
#   The tool.
describe_tool() {
  version_args=(--version) source=uv without_exe='' rock=''
  case "$1" in
    ruff)
      version_name=RUFF_VERSION command=ruff package="ruff==${RUFF_VERSION}"
      expected="ruff ${RUFF_VERSION}"
      ;;
    shfmt)
      # shfmt-py's version is the wheel's, not the shfmt it packages.
      version_name=SHFMT_VERSION command=shfmt
      package="shfmt-py==${SHFMT_VERSION}" expected=
      ;;
    shellcheck)
      version_name=SHELLCHECK_VERSION command=shellcheck
      package="shellcheck-py==${SHELLCHECK_VERSION}"
      expected="version: ${SHELLCHECK_VERSION%.*}"
      ;;
    actionlint)
      version_name=ACTIONLINT_VERSION command=actionlint
      package="actionlint-py==${ACTIONLINT_VERSION}"
      expected="${ACTIONLINT_VERSION%.*}"
      ;;
    gersemi)
      version_name=GERSEMI_VERSION command=gersemi
      package="gersemi==${GERSEMI_VERSION}"
      expected="gersemi ${GERSEMI_VERSION}"
      ;;
    pymarkdown)
      version_name=PYMARKDOWN_VERSION command=pymarkdown
      package="pymarkdownlnt==${PYMARKDOWN_VERSION}"
      version_args=(version) expected="${PYMARKDOWN_VERSION}"
      ;;
    yamllint)
      version_name=YAMLLINT_VERSION command=yamllint
      package="yamllint==${YAMLLINT_VERSION}"
      expected="yamllint ${YAMLLINT_VERSION}"
      ;;
    ctcache)
      # A commit, which clang-tidy-cache does not report: --help shows it runs.
      version_name=CTCACHE_COMMIT command=clang-tidy-cache
      package="ctcache @ git+https://github.com/matus-chochlik/ctcache@${CTCACHE_COMMIT}"
      version_args=(--help) expected=
      ;;
    node)
      # Node from its PyPI wheel, which runs Prettier: uv's launcher of it.
      version_name=NODEJS_WHEEL_VERSION command=node
      package="nodejs-wheel==${NODEJS_WHEEL_VERSION}"
      expected="v${NODEJS_WHEEL_VERSION}"
      ;;
    yamlfmt)
      version_name=YAMLFMT_VERSION command=yamlfmt source=release
      expected="yamlfmt ${YAMLFMT_VERSION}"
      ;;
    stylua)
      version_name=STYLUA_VERSION command=stylua source=release
      expected="stylua ${STYLUA_VERSION}"
      ;;
    luacheck)
      # No release for macOS or Linux arm64, where its rock builds it.
      version_name=LUACHECK_VERSION command=luacheck source=release
      rock=luacheck
      expected="Luacheck: ${LUACHECK_VERSION}"
      ;;
    taplo)
      version_name=TAPLO_VERSION command=taplo source=release
      expected="taplo ${TAPLO_VERSION}"
      ;;
    buf)
      # buf's extension runs the exact path its setting names, which has no
      # .exe on Windows.
      version_name=BUF_VERSION command=buf source=release without_exe=1
      expected="${BUF_VERSION}"
      ;;
    # The tools CI's own jobs run, and the operators' flux and cosign.
    trivy)
      version_name=TRIVY_VERSION command=trivy source=release
      expected="Version: ${TRIVY_VERSION}"
      ;;
    kubeconform)
      version_name=KUBECONFORM_VERSION command=kubeconform source=release
      version_args=(-v) expected="v${KUBECONFORM_VERSION}"
      ;;
    kyverno)
      version_name=KYVERNO_VERSION command=kyverno source=release
      version_args=(version) expected="Version: ${KYVERNO_VERSION}"
      ;;
    promtool)
      version_name=PROMETHEUS_VERSION command=promtool source=release
      expected="promtool, version ${PROMETHEUS_VERSION}"
      ;;
    amtool)
      version_name=ALERTMANAGER_VERSION command=amtool source=release
      expected="amtool, version ${ALERTMANAGER_VERSION}"
      ;;
    hadolint)
      version_name=HADOLINT_VERSION command=hadolint source=release
      expected="Haskell Dockerfile Linter ${HADOLINT_VERSION}"
      ;;
    oras)
      version_name=ORAS_VERSION command=oras source=release
      version_args=(version) expected="Version:        ${ORAS_VERSION}"
      ;;
    syft)
      version_name=SYFT_VERSION command=syft source=release
      expected="syft ${SYFT_VERSION}"
      ;;
    grype)
      version_name=GRYPE_VERSION command=grype source=release
      expected="grype ${GRYPE_VERSION}"
      ;;
    yq)
      version_name=YQ_VERSION command=yq source=release
      expected="yq (https://github.com/mikefarah/yq/) version v${YQ_VERSION}"
      ;;
    flux)
      version_name=FLUX_VERSION command=flux source=release
      expected="flux version ${FLUX_VERSION}"
      ;;
    cosign)
      version_name=COSIGN_VERSION command=cosign source=release
      version_args=(version) expected="GitVersion:    v${COSIGN_VERSION}"
      ;;
    *)
      echo "toolchain: unknown tool $1" >&2
      exit 1
      ;;
  esac
  version="${!version_name}"
}

# Sets the global major to the LLVM major a tool's --version output names
# ("clang-format version 21.1.8", "LLVM version 21.1.8", "Apple clangd version
# 17.0.0"), or to nothing.
# Arguments:
#   The output.
read_major() {
  local pattern='version ([0-9]+)\.'
  major=
  if [[ "$1" =~ ${pattern} ]]; then
    major="${BASH_REMATCH[1]}"
  fi
}
