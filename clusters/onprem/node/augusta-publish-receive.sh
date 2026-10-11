#!/bin/bash
# The publish key's forced command (ADR-0057): all that key does on the Node,
# whatever command its holder asks for. scripts/bootstrap-node.sh installs it
# as /usr/local/bin/augusta-publish-receive, root's, and names it in the
# augusta-publish user's authorized_keys; sshd runs it as that user, the
# command asked for in SSH_ORIGINAL_COMMAND, which is checked here word by word
# and never run.
#
# augusta-publish (tools/pack) is its one client, and a publish is one
# connection, so one touch of a FIDO2 key:
#
#   publish <folder> server.pack=<sha256>:<bytes> signing.pub=<sha256>:<bytes>
#
# A folder is <scenario>/<version> under the pack volume. stdin holds the
# files back to back, in the order the command names them, each exactly its
# size: no archive is parsed here, and the names are the command's alone. They
# are written to the folder's staging place beside it, checked against their
# sizes and digests, made read-only and renamed into the folder.
#
# A folder that exists is never written again: publish reads nothing into the
# volume for it, and exits 3 when it holds exactly the files named, 1 when it
# holds others. A published folder is 0550 with its files 0440, read-only to
# this user too. The user owns them and could change their modes back, but its
# key runs nothing but this.
# The volume's setgid group, the servers' (k3s-node-recovery.md, The server's
# user), passes to everything created in it. Exits 0 once published, 2 for
# anything else asked, 1 when a publish fails, leaving no folder and no staging
# place.
set -euo pipefail

readonly PACKS=/srv/augusta/asset-packs
readonly PUBLISHED=3
# A scenario as augusta-publish takes one, and a version: the first 12 hex
# characters of the server pack's BLAKE3 hash.
readonly FOLDER_PATTERN='^[a-z0-9]([a-z0-9-]{0,44}[a-z0-9])?/[0-9a-f]{12}$'
readonly NAMES=(server.pack signing.pub)
# <sha256>:<bytes>, the size in decimal with no leading zero.
readonly FILE_PATTERN='^[0-9a-f]{64}:(0|[1-9][0-9]{0,14})$'

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
#   <name>=<sha256>:<bytes> for each file a pack folder holds, as the client
#   named them.
check_files() {
  local pair given
  for pair in "$@"; do
    if [[ ! "${pair#*=}" =~ ${FILE_PATTERN} ]]; then
      refuse "not <sha256>:<bytes> for ${pair%%=*}: ${pair#*=}"
    fi
  done
  # Every file a pack folder holds, each once.
  given="$(printf '%s\n' "${@%%=*}" | sort)"
  if [[ "${given}" != "$(printf '%s\n' "${NAMES[@]}" | sort)" ]]; then
    refuse "a publish names ${NAMES[*]}, not: ${*%%=*}"
  fi
}

# Arguments:
#   <name>=<sha256>:<bytes> for each file.
# Outputs:
#   Their digests as sha256sum prints them, sorted by name.
expected_digests() {
  local pair digest
  for pair in "$@"; do
    digest="${pair#*=}"
    printf '%s  %s\n' "${digest%%:*}" "${pair%%=*}"
  done | sort -k 2
}

# Arguments:
#   A directory.
# Outputs:
#   The digests of the files in it as sha256sum prints them, sorted by name.
held_digests() {
  (cd "$1" && sha256sum -- *) | sort -k 2
}

# Publishes a folder from stdin, or exits: 3 when it is published already with
# those files, 1 when it holds others or stdin is not the files named.
# Globals:
#   STAGING, set to the folder's staging place, which the EXIT trap removes.
# Arguments:
#   A folder, then <name>=<sha256>:<bytes> for each file, in stdin's order.
publish() {
  local folder="$1" expected pair size offset=0 total=0
  shift
  expected="$(expected_digests "$@")"
  if [[ -e "${PACKS}/${folder}" ]]; then
    if [[ "$(held_digests "${PACKS}/${folder}")" == "${expected}" ]]; then
      log "${PACKS}/${folder} is published already, with those files."
      exit "${PUBLISHED}"
    fi
    log "${PACKS}/${folder} is published already and holds other files;" \
      "it is never written again."
    exit 1
  fi

  # A hidden sibling no server is pointed at, left by no failure: a folder is
  # there whole or not at all.
  STAGING="${PACKS}/${folder%/*}/.${folder#*/}.partial"
  # shellcheck disable=SC2064 # STAGING is set once, here
  trap "rm -rf -- '${STAGING}'" EXIT
  rm -rf -- "${STAGING}"
  mkdir -p "${STAGING}"
  for pair in "$@"; do
    size="${pair##*:}"
    total=$((total + size))
  done
  # One byte past the files named, to tell a longer stdin from theirs; never
  # more, whatever is sent.
  head -c "$((total + 1))" >"${STAGING}/.stream"
  if [[ "$(stat -c %s "${STAGING}/.stream")" != "${total}" ]]; then
    log "stdin for ${folder} is not ${total} bytes, the files named."
    exit 1
  fi
  for pair in "$@"; do
    size="${pair##*:}"
    dd if="${STAGING}/.stream" of="${STAGING}/${pair%%=*}" bs=64K \
      skip="${offset}" count="${size}" iflag=skip_bytes,count_bytes \
      status=none
    offset=$((offset + size))
  done
  rm -- "${STAGING}/.stream"
  if [[ "$(held_digests "${STAGING}")" != "${expected}" ]]; then
    log "what was received for ${folder} is not the files named."
    exit 1
  fi
  # Read-only before it is renamed into place, which within one folder needs
  # no write permission on it: never writable once published.
  chmod 0440 -- "${STAGING}"/*
  chmod 0550 -- "${STAGING}"
  mv -T -- "${STAGING}" "${PACKS}/${folder}"
}

# Runs the command the client asked for, or exits 2.
# Globals:
#   SSH_ORIGINAL_COMMAND, the command, which sshd sets.
main() {
  local words
  if [[ "${SSH_ORIGINAL_COMMAND:-}" == *$'\n'* ]]; then
    refuse "one command per connection, on one line."
  fi
  # Split on blanks, never expanded: no word is a glob or a variable here.
  read -r -a words <<<"${SSH_ORIGINAL_COMMAND:-}"
  case "${words[0]:-} ${#words[@]}" in
    "publish 4")
      check_folder "${words[1]}"
      check_files "${words[@]:2}"
      publish "${words[@]:1}"
      ;;
    *)
      refuse "only publish, not: ${SSH_ORIGINAL_COMMAND:-}"
      ;;
  esac
}

main "$@"
