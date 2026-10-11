#!/bin/bash
# The publish key's forced command (ADR-0057): all that key does on the Node,
# whatever command its holder asks for. scripts/bootstrap-node.sh installs it
# as /usr/local/bin/augusta-publish-receive, root's, and names it in the
# augusta-publish user's authorized_keys; sshd runs it as that user, the
# command asked for in SSH_ORIGINAL_COMMAND, which is checked here word by word
# and never run.
#
# augusta-publish (tools/pack) is its one client. A folder is
# <scenario>/<version> under the pack volume:
#
#   digests <folder>            the SHA-256 of each file in it, as sha256sum
#                               prints them; exits 3 when it does not exist.
#   receive <folder> <name>     stdin into <name>, server.pack or signing.pub,
#                               in the folder's staging place beside it.
#   commit <folder> server.pack=<sha256> signing.pub=<sha256>
#                               the staging place, holding exactly those files
#                               with those digests, made read-only and renamed
#                               into the folder.
#
# A folder that exists is never written again: receive and commit refuse it,
# and commit leaves it 0550 with its files 0440, read-only to this user too.
# The user owns them and could change their modes back, but its key runs
# nothing but this.
# The volume's setgid group, the servers' (k3s-node-recovery.md, The server's
# user), passes to everything created in it. Exits 2 for anything else asked,
# 1 when a command fails.
set -euo pipefail

readonly PACKS=/srv/augusta/asset-packs
readonly MISSING=3
# A scenario as augusta-publish takes one, and a version: the first 12 hex
# characters of the server pack's BLAKE3 hash.
readonly FOLDER_PATTERN='^[a-z0-9]([a-z0-9-]{0,44}[a-z0-9])?/[0-9a-f]{12}$'
readonly NAMES=(server.pack signing.pub)
readonly DIGEST_PATTERN='^[0-9a-f]{64}$'

# Group-readable only: the servers' group reads, no one else.
umask 0027

log() {
  echo "augusta-publish-receive: $*" >&2
}

refuse() {
  log "$*"
  exit 2
}

# Arguments:
#   A folder, as the client named it.
check_folder() {
  if [[ ! "$1" =~ ${FOLDER_PATTERN} ]]; then
    refuse "not a <scenario>/<version> folder: $1"
  fi
}

# Arguments:
#   A file name, as the client named it.
check_name() {
  local name
  for name in "${NAMES[@]}"; do
    if [[ "$1" == "${name}" ]]; then
      return
    fi
  done
  refuse "not a file a pack folder holds: $1"
}

# Arguments:
#   A folder.
# Outputs:
#   Its staging place: a hidden sibling no server is pointed at.
staging_of() {
  echo "${PACKS}/${1%/*}/.${1#*/}.partial"
}

# Exits 1 when a folder is published already.
# Arguments:
#   A folder.
refuse_published() {
  if [[ -e "${PACKS}/$1" ]]; then
    log "${PACKS}/$1 is published already and is never written again."
    exit 1
  fi
}

# Arguments:
#   A folder.
# Outputs:
#   The SHA-256 of each file in it, as sha256sum prints them. Exits 3 when it
#   does not exist.
digests() {
  local folder="${PACKS}/$1"
  if [[ ! -d "${folder}" ]]; then
    exit "${MISSING}"
  fi
  cd "${folder}"
  sha256sum -- *
}

# Arguments:
#   A folder, a file name.
receive() {
  local staging
  refuse_published "$1"
  staging="$(staging_of "$1")"
  mkdir -p "${PACKS}/${1%/*}" "${staging}"
  cat >"${staging}/$2"
}

# Publishes a folder from its staging place, or exits 1 and removes the
# staging place when that does not hold exactly the files named.
# Arguments:
#   A folder, then <name>=<sha256> for each file it holds.
commit() {
  local folder="$1" staging expected="" received="" pair
  shift
  refuse_published "${folder}"
  staging="$(staging_of "${folder}")"
  # Both as sha256sum prints them, sorted by name.
  for pair in "$@"; do
    expected+="${pair#*=}  ${pair%%=*}"$'\n'
  done
  expected="$(printf '%s' "${expected}" | sort -k 2)"
  if [[ -d "${staging}" ]]; then
    received="$(cd "${staging}" && sha256sum -- * | sort -k 2)" || true
  fi
  if [[ -z "${received}" || "${received}" != "${expected}" ]]; then
    log "what was received for ${folder} is not the files named."
    rm -rf -- "${staging}"
    exit 1
  fi
  # Read-only before it is renamed into place, which within one folder needs
  # no write permission on it: never writable once published.
  chmod 0440 -- "${staging}"/*
  chmod 0550 -- "${staging}"
  mv -T -- "${staging}" "${PACKS}/${folder}"
}

# Runs the command the client asked for, or exits 2.
# Globals:
#   SSH_ORIGINAL_COMMAND, the command, which sshd sets.
main() {
  local words names=() given pair
  if [[ "${SSH_ORIGINAL_COMMAND:-}" == *$'\n'* ]]; then
    refuse "one command per connection, on one line."
  fi
  # Split on blanks, never expanded: no word is a glob or a variable here.
  read -r -a words <<<"${SSH_ORIGINAL_COMMAND:-}"
  case "${words[0]:-} ${#words[@]}" in
    "digests 2")
      check_folder "${words[1]}"
      digests "${words[1]}"
      ;;
    "receive 3")
      check_folder "${words[1]}"
      check_name "${words[2]}"
      receive "${words[1]}" "${words[2]}"
      ;;
    "commit 4")
      check_folder "${words[1]}"
      for pair in "${words[@]:2}"; do
        names+=("${pair%%=*}")
        if [[ ! "${pair#*=}" =~ ${DIGEST_PATTERN} ]]; then
          refuse "not a SHA-256 for ${pair%%=*}: ${pair#*=}"
        fi
      done
      # Every file a pack folder holds, each once.
      given="$(printf '%s\n' "${names[@]}" | sort)"
      if [[ "${given}" != "$(printf '%s\n' "${NAMES[@]}" | sort)" ]]; then
        refuse "a commit names ${NAMES[*]}, not: ${names[*]}"
      fi
      commit "${words[@]:1}"
      ;;
    *)
      refuse "only digests, receive and commit, not:" \
        "${SSH_ORIGINAL_COMMAND:-}"
      ;;
  esac
}

main "$@"
