#!/bin/bash
# Launches the Augusta USD Composer app (ADR-0015), built by
# tools/composer/scripts/bootstrap.sh, on Windows (in Git Bash) or Linux. A
# thin wrapper around kit-app-template's own `repo launch`, run with its
# working directory set to <assets-root>/tools/kit-app-template (the repo
# tool's own scripts assume that), located relative to this script's own path
# so it works wherever the assets root lives - copied into <assets-root>/bin
# by the bootstrap. Arguments are forwarded as-is, e.g.
# `augusta-composer.sh --name augusta.kit` if `repo launch` asks which app
# when more than one is registered.
set -euo pipefail

kit_app_template_dir="$(cd "$(dirname "$0")/.." && pwd)/tools/kit-app-template"
case "$(uname -s)" in
  MINGW* | MSYS* | CYGWIN*) repo=repo.bat ;;
  *) repo=repo.sh ;;
esac
if [[ ! -f "${kit_app_template_dir}/${repo}" ]]; then
  echo "augusta-composer: ${kit_app_template_dir}/${repo} not found - run" \
    "tools/composer/scripts/bootstrap.sh first." >&2
  exit 1
fi

cd "${kit_app_template_dir}"
exec "./${repo}" launch "$@"
