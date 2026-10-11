#!/bin/bash
# Checks or records augustad's seccomp profile
# (clusters/onprem/node/augustad-seccomp.json, ADR-0057): the syscalls the
# server image makes, its tini and the container runtime's last steps before
# them included, and nothing else. The Node installs it as the Servers'
# Localhost profile (docs/runbooks/k3s-node-recovery.md, The server's seccomp
# profile), which kills the server at its first syscall outside it.
#
# Both run the image as the chart runs it (its user, every capability dropped,
# a read-only root, no new privileges), serving the example scenario's pack,
# with its metrics endpoint scraped, while Agents play two full Matches against
# it and it captures them, keeping one capture alone, so retention deletes the
# other; then stop it as the kubelet does, with SIGTERM.
#
#   check   Runs it under the profile. Fails when the server does not start,
#           finish the Matches, write a capture or exit 0 on SIGTERM: a
#           syscall outside the profile kills it, and the kernel log names it.
#   record  Runs it under the profile with logging in place of killing, adds
#           each syscall the audit log then holds to the profile, and runs
#           again until a run logs none. A profile only grows: one the Node
#           has while servers of an older image still run (staging's) must
#           still be theirs. Dropping a syscall is an edit by hand, once no
#           Environment runs an image that makes it.
#
# The profile is the Node's architecture's alone, x86-64: run this on an
# x86-64 Linux host (CI's confinement job does, and uploads what record
# writes), with Docker, sudo, and for record auditd, after `uv sync --group
# agent` in tools/ and a build of the Agents' native module (ci.yml's server
# job). The image is the one CI built, or `docker build -t <image> .`.
#
# Usage: scripts/augustad-seccomp.sh check|record <image>
set -euo pipefail

TOP="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly TOP
readonly PROFILE="${TOP}/clusters/onprem/node/augustad-seccomp.json"
readonly PACKS="${TOP}/tests/fixtures/example-packs"
# The image's user (ADR-0054), which owns the capture directory.
readonly SERVER_ID=65532
readonly CONTAINER=augustad-seccomp
readonly GAME_PORT=7777 METRICS_PORT=9464
readonly MATCHES=2
# Runs record gives up after, should every one log something new.
readonly RECORD_RUNS=5
# What a Match does not reach but a server may still make: its crash handler
# (src/server/crash.cpp) re-raising a fatal signal (rt_sigprocmask, getpid,
# gettid, tgkill) after symbolizing the stack from its own binary (pread64,
# lseek). Without them a crash would end in SIGSYS, its report and core lost.
readonly CRASH_SYSCALLS=(rt_sigprocmask getpid gettid tgkill pread64 lseek)
readonly AUDIT_LOG=/var/log/audit/audit.log

log() {
  echo "augustad-seccomp: $*" >&2
}

# Prints how to run it, and exits.
usage() {
  echo "usage: $0 check|record <image>" >&2
  exit 2
}

# Writes the server's settings, its pack and its capture directory to a new
# directory, removed with the container when the script exits.
# Globals:
#   WORK, set to that directory.
prepare() {
  WORK="$(mktemp -d)"
  readonly WORK
  # shellcheck disable=SC2064 # WORK is set once, here
  trap "docker rm -f '${CONTAINER}' >/dev/null 2>&1 || true
    sudo rm -rf -- '${WORK}'" EXIT
  mkdir "${WORK}/config" "${WORK}/pack"
  cp "${PACKS}/server.pack" "${WORK}/pack/server.pack"
  cp "${PACKS}/test.pub" "${WORK}/pack/signing.pub"
  sudo install -d -o "${SERVER_ID}" -g "${SERVER_ID}" "${WORK}/captures"
  # As the chart renders it (charts/augustad/templates/_helpers.tpl).
  cat >"${WORK}/config/augustad.yaml" <<EOF
base_dir: /srv/augusta/pack
content:
  pack: server.pack
  public_key: signing.pub
simulation:
  tick_rate_hz: 60
  capture: /srv/augusta/captures/develop/example
  capture_retention:
    max_files: 1
network:
  listen_address: 0.0.0.0:${GAME_PORT}
metrics:
  port: ${METRICS_PORT}
logging:
  level: info
EOF
}

# Plays the Matches against the image under a seccomp profile and stops it,
# printing the server's log when it fails.
# Globals:
#   WORK
# Arguments:
#   The image, the profile.
# Returns:
#   0 when the server served them, captured them and exited 0.
play() {
  local image="$1" profile="$2"
  docker rm -f "${CONTAINER}" >/dev/null 2>&1 || true
  sudo find "${WORK}/captures" -mindepth 1 -delete
  # Under a profile that lacks the runtime's own last syscalls, it fails here.
  if ! docker run --detach --name "${CONTAINER}" \
    --user "${SERVER_ID}:${SERVER_ID}" --read-only --cap-drop ALL \
    --security-opt no-new-privileges --security-opt "seccomp=${profile}" \
    --publish "127.0.0.1:${GAME_PORT}:${GAME_PORT}/udp" \
    --publish "127.0.0.1:${METRICS_PORT}:${METRICS_PORT}" \
    --volume "${WORK}/config:/etc/augusta:ro" \
    --volume "${WORK}/pack:/srv/augusta/pack:ro" \
    --volume "${WORK}/captures:/srv/augusta/captures" \
    "${image}" --config /etc/augusta/augustad.yaml >/dev/null; then
    log "the server's container did not start"
    return 1
  fi

  local status=0
  # /livez answers once the pack is verified and loaded (ADR-0049), as the
  # startup probe waits for.
  if ! curl -fsS --retry 30 --retry-delay 1 --retry-all-errors \
    "http://127.0.0.1:${METRICS_PORT}/livez" >/dev/null; then
    log "the server never answered /livez"
    status=1
  elif ! (cd "${TOP}/tools" && uv run --no-sync python \
    agent/examples/load_test.py --server "127.0.0.1:${GAME_PORT}" \
    --pack "${PACKS}/client.pack" --public-key "${PACKS}/test.pub" \
    --matches "${MATCHES}" >/dev/null); then
    log "the Agents did not finish ${MATCHES} Matches"
    status=1
  elif ! curl -fsS "http://127.0.0.1:${METRICS_PORT}/metrics" >/dev/null; then
    log "the server stopped answering /metrics"
    status=1
  fi

  docker stop --time 10 "${CONTAINER}" >/dev/null || true
  local exit_code
  exit_code="$(docker inspect --format '{{.State.ExitCode}}' "${CONTAINER}")"
  if [[ "${exit_code}" != 0 ]]; then
    # 159: 128 + SIGSYS, a syscall outside the profile.
    log "the server exited ${exit_code}"
    status=1
  elif [[ "${status}" = 0 && -z "$(sudo find "${WORK}/captures" \
    -type f -name '*.capture' -print -quit)" ]]; then
    log "the server wrote no capture"
    status=1
  fi
  if [[ "${status}" != 0 ]]; then
    docker logs "${CONTAINER}" >&2 || true
  fi
  return "${status}"
}

# Plays the Matches under the profile; exits 1, naming what the kernel killed,
# when the server fails.
# Arguments:
#   The image.
check() {
  if ! play "$1" "${PROFILE}"; then
    # The kill's audit record names the syscall by number (syscall=), on
    # x86-64 as in /usr/include/x86_64-linux-gnu/asm/unistd_64.h.
    sudo dmesg | grep 'type=1326' | tail -n 5 >&2 || true
    log "the server failed under its seccomp profile; if it was killed" \
      "(exit 159), record the profile again: $0 record <image>"
    exit 1
  fi
  log "the server played ${MATCHES} Matches under its seccomp profile"
}

# Globals:
#   WORK, AUDIT_LOG
# Arguments:
#   A line of the audit log.
# Outputs:
#   The syscalls it recorded seccomp logging after that line, one per line,
#   sorted; nothing when it recorded none.
logged_since() {
  # shellcheck disable=SC2024 # the copy is ours; reading the log is root's
  sudo tail -n "+$(($1 + 1))" "${AUDIT_LOG}" >"${WORK}/audit.log"
  { ausearch --input "${WORK}/audit.log" --message SECCOMP --interpret \
    2>/dev/null || true; } \
    | grep -o 'syscall=[a-z0-9_]*' | cut -d = -f 2 | sort -u
}

# Adds syscalls to the profile, in place, keeping its names sorted and unique.
# Globals:
#   PROFILE, WORK
# Arguments:
#   A file of syscall names, one per line.
add_to_profile() {
  jq --rawfile added "$1" '
    .syscalls[0].names = (.syscalls[0].names
      + ($added | split("\n") | map(select(. != ""))) | unique)' \
    "${PROFILE}" >"${WORK}/profile.json"
  mv "${WORK}/profile.json" "${PROFILE}"
}

# Records the profile, then checks the server runs under it; exits 1 when it
# cannot.
# Globals:
#   PROFILE, WORK, AUDIT_LOG
# Arguments:
#   The image.
record() {
  local run start
  # Room for every record of a run, and none dropped for rate.
  sudo auditctl -b 65536 -r 0 >/dev/null
  # Else every run would log nothing, and record take the profile as complete.
  if ! sudo auditctl -s | grep -q '^enabled 1'; then
    log "auditd is not running: nothing would be recorded"
    exit 1
  fi
  printf '%s\n' "${CRASH_SYSCALLS[@]}" >"${WORK}/added"
  add_to_profile "${WORK}/added"
  for ((run = 1; run <= RECORD_RUNS; run++)); do
    jq '.defaultAction = "SCMP_ACT_LOG"' "${PROFILE}" >"${WORK}/logging.json"
    start="$(sudo cat "${AUDIT_LOG}" | wc -l)"
    if ! play "$1" "${WORK}/logging.json"; then
      log "run ${run} failed with nothing killed: see the server's log above"
      exit 1
    fi
    logged_since "${start}" >"${WORK}/added"
    if [[ ! -s "${WORK}/added" ]]; then
      log "run ${run} logged nothing new; checking ${PROFILE}"
      check "$1"
      return
    fi
    log "run ${run} logged: $(tr '\n' ' ' <"${WORK}/added")"
    add_to_profile "${WORK}/added"
  done
  log "every run logged something new; run it again"
  exit 1
}

main() {
  if (($# != 2)); then
    usage
  fi
  case "$1" in
    check | record) ;;
    *) usage ;;
  esac
  prepare
  "$1" "$2"
}

main "$@"
