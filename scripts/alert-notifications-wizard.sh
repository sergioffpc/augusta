#!/bin/bash
# Walks the operator through Alertmanager's Telegram notifications (ADR-0049,
# docs/runbooks/alert-notifications.md), one stage at a time: makes the bot
# with BotFather, finds the chat it posts to, has the bot post there, makes
# the Secret monitoring/alertmanager-telegram on the node, and once
# Alertmanager mounts it, raises a test alert. The token and the chat's id stay
# in this shell's memory and reach the node over SSH's stdin alone: nothing is
# written on the workstation. Run it before the change that mounts the Secret
# merges, and again after the node is rebuilt; a run replaces the Secret.
# Ctrl-C stops it at any stage.
#
# Usage: scripts/alert-notifications-wizard.sh
set -euo pipefail

if [[ -t 1 ]] && command -v tput >/dev/null 2>&1 \
  && [[ "$(tput colors 2>/dev/null || echo 0)" -ge 8 ]]; then
  BOLD=$(tput bold)
  DIM=$(tput dim)
  RESET=$(tput sgr0)
  BLUE=$(tput setaf 4)
  GREEN=$(tput setaf 2)
  YELLOW=$(tput setaf 3)
else
  BOLD=""
  DIM=""
  RESET=""
  BLUE=""
  GREEN=""
  YELLOW=""
fi

readonly ALERTMANAGER=alertmanager-kube-prometheus-stack-alertmanager
readonly RUNBOOK=docs/runbooks/alert-notifications.md
readonly TOTAL_STAGES=5
STAGE_INDEX=0
# What is left for the operator to do by hand, shown at the end.
SKIPPED=()

# Wipes the terminal so only the current step is on screen; nothing when
# output is not a terminal, so a piped log stays readable.
clear_screen() {
  [[ -t 1 ]] || return 0
  if command -v tput >/dev/null 2>&1 && tput clear 2>/dev/null; then
    return 0
  fi
  printf '\033[2J\033[3J\033[H'
}

# Print a line of the stage: an instruction, an action the operator takes in
# Telegram, an aside, or a warning.
# Globals:
#   BLUE, DIM, YELLOW, RESET
# Arguments:
#   The line.
# Outputs:
#   The line, to stdout.
say() { printf '  %s\n' "$1"; }
step() { printf '  %s•%s %s\n' "${BLUE}" "${RESET}" "$1"; }
note() { printf '  %s%s%s\n' "${DIM}" "$1" "${RESET}"; }
warn() { printf '  %s⚠ %s%s\n' "${YELLOW}" "$1" "${RESET}"; }

# Waits for Enter.
# Globals:
#   DIM, RESET
# Arguments:
#   The prompt, optional.
pause() {
  printf '  %s%s%s ' "${DIM}" "${1:-Press Enter to continue}" "${RESET}"
  read -r _ || true
}

# A y/N gate.
# Globals:
#   YELLOW, RESET
# Arguments:
#   The question.
# Returns:
#   0 on yes, 1 otherwise.
confirm() {
  local reply=""
  printf '  %s? %s [y/N]%s ' "${YELLOW}" "$1" "${RESET}"
  read -r reply || true
  [[ "${reply}" =~ ^[Yy] ]]
}

# The opening frame.
# Globals:
#   BOLD, BLUE, DIM, RESET, TOTAL_STAGES
# Arguments:
#   The title.
banner() {
  clear_screen
  printf '\n%s%s  %s%s\n' "${BOLD}" "${BLUE}" "$1" "${RESET}"
  printf '%s  %s stages%s\n\n' "${DIM}" "${TOTAL_STAGES}" "${RESET}"
  note "You drive Telegram; this wizard says what to do there and takes the"
  note "values you copy back. Stop any time with Ctrl-C."
  pause "Ready to start?"
}

# Clears the screen and announces the next stage.
# Globals:
#   STAGE_INDEX (written), TOTAL_STAGES, BOLD, BLUE, RESET
# Arguments:
#   The stage's name.
stage() {
  clear_screen
  STAGE_INDEX=$((STAGE_INDEX + 1))
  printf '\n%s%s▸ Stage %s/%s · %s%s\n' \
    "${BOLD}" "${BLUE}" "${STAGE_INDEX}" "${TOTAL_STAGES}" "$1" "${RESET}"
}

# Opens a URL in the workstation's browser, WSL's included.
# Globals:
#   GREEN, RESET
# Arguments:
#   The URL.
open_url() {
  local -r url="$1"
  printf '  %s↗ opening%s %s\n' "${GREEN}" "${RESET}" "${url}"
  {
    if command -v wslview >/dev/null 2>&1; then
      wslview "${url}"
    elif command -v explorer.exe >/dev/null 2>&1; then
      explorer.exe "${url}"
    elif command -v xdg-open >/dev/null 2>&1; then
      xdg-open "${url}"
    elif command -v open >/dev/null 2>&1; then
      open "${url}"
    else
      false
    fi
  } >/dev/null 2>&1 || warn "couldn't open a browser: visit ${url}"
}

# Reads a value, shown as it is typed, into a variable.
# Arguments:
#   The variable's name (written), the prompt.
ask() {
  local input
  read -e -r -p "  $2 " input
  printf -v "$1" '%s' "${input}"
}

# Reads a value, hidden as it is typed, into a variable.
# Globals:
#   BOLD, RESET
# Arguments:
#   The variable's name (written), the prompt.
ask_secret() {
  local input
  printf '  %s%s%s ' "${BOLD}" "$2" "${RESET}"
  read -rs input
  printf '\n'
  printf -v "$1" '%s' "${input}"
}

# The closing summary.
# Globals:
#   SKIPPED, BOLD, GREEN, RESET
# Outputs:
#   What is left to do by hand, to stdout.
finish() {
  clear_screen
  printf '\n%s%s  ✓ Done%s\n' "${BOLD}" "${GREEN}" "${RESET}"
  if ((${#SKIPPED[@]})); then
    printf '\n'
    warn "still to do by hand:"
    local item
    for item in "${SKIPPED[@]}"; do
      note "  - ${item}"
    done
  fi
  printf '\n'
}

# Calls the Bot API. curl reads the URL, which holds the token, from stdin
# rather than from a command line ps shows.
# Globals:
#   TELEGRAM_BOT_TOKEN
# Arguments:
#   The method, then curl's further arguments.
# Outputs:
#   The API's JSON answer, to stdout.
telegram() {
  local -r method="$1"
  shift
  printf 'url = "https://api.telegram.org/bot%s/%s"\n' \
    "${TELEGRAM_BOT_TOKEN}" "${method}" \
    | curl --fail --silent --show-error --config - "$@"
}

# Runs a script with bash on the node, as the SSH user, whose sudo is k3s's
# cluster-admin (docs/runbooks/k3s-node-recovery.md, Cluster access).
# Globals:
#   NODE
# Arguments:
#   The script.
# Outputs:
#   The script's output.
on_node() {
  printf '%s\n' "$1" | ssh "${NODE}" bash -s
}

# The script that makes the Secret on the node, or replaces it, from files
# only the SSH user reads, for as long as kubectl reads them: neither value is
# ever on a command line.
# Globals:
#   TELEGRAM_BOT_TOKEN, TELEGRAM_CHAT_ID
# Outputs:
#   The remote script, to stdout.
secret_script() {
  local token_q chat_q
  printf -v token_q '%q' "${TELEGRAM_BOT_TOKEN}"
  printf -v chat_q '%q' "${TELEGRAM_CHAT_ID}"
  cat <<EOF
set -euo pipefail
dir="\$(mktemp -d)"
trap 'rm -rf "\${dir}"' EXIT
printf '%s' ${token_q} > "\${dir}/bot-token"
printf '%s' ${chat_q} > "\${dir}/chat-id"
kubectl() { sudo k3s kubectl -n monitoring "\$@"; }
verb=create
if kubectl get secret alertmanager-telegram >/dev/null 2>&1; then
  verb=replace
fi
kubectl create secret generic alertmanager-telegram \\
  --from-file=bot-token="\${dir}/bot-token" \\
  --from-file=chat-id="\${dir}/chat-id" \\
  --dry-run=client -o yaml \\
  | kubectl "\${verb}" -f -
EOF
}

# The script that waits on the node for Alertmanager, then raises a critical
# alert in it that ends in 5 minutes.
# Globals:
#   ALERTMANAGER
# Outputs:
#   The remote script, to stdout.
test_alert_script() {
  cat <<EOF
set -euo pipefail
kubectl() { sudo k3s kubectl -n monitoring "\$@"; }
kubectl rollout status statefulset/${ALERTMANAGER} --timeout=5m
kubectl exec ${ALERTMANAGER}-0 -c alertmanager -- \\
  amtool alert add AlertNotificationTest severity=critical \\
  --annotation=summary="A test of the alert notifications." \\
  --end="\$(date -u -d '+5 minutes' +%Y-%m-%dT%H:%M:%SZ)" \\
  --alertmanager.url=http://localhost:9093
EOF
}

# Globals:
#   TELEGRAM_BOT_TOKEN, TELEGRAM_CHAT_ID, NODE (all written), SKIPPED
#   (written), RUNBOOK
main() {
  banner "Telegram alert notifications"

  stage "Create the bot"
  say "BotFather makes the bot Alertmanager posts as."
  open_url "https://t.me/BotFather"
  step "Send /newbot."
  step "Give it a name, e.g. Augusta alerts."
  step "Give it a username ending in bot, e.g. augusta_alerts_bot."
  step "Copy the token BotFather answers with: digits, a colon, letters."
  ask_secret TELEGRAM_BOT_TOKEN "Paste the bot's token:"
  local me bot_username
  me=$(telegram getMe)
  bot_username=$(printf '%s' "${me}" | grep -o '"username":"[^"]*"' \
    | head -n 1 | cut -d '"' -f 4)
  say "The token is valid: the bot is @${bot_username}."
  pause

  stage "Find the chat's id"
  say "The bot can only post to a chat that has written to it first."
  open_url "https://t.me/${bot_username}"
  step "Press Start in the bot's chat, then send it any message."
  pause "Press Enter once you have sent it."
  local updates
  updates=$(telegram getUpdates)
  TELEGRAM_CHAT_ID=$(printf '%s' "${updates}" \
    | grep -o '"chat":{"id":-\{0,1\}[0-9]*' | tail -n 1 \
    | grep -o -- '-\{0,1\}[0-9]*$' || true)
  if [[ -z "${TELEGRAM_CHAT_ID}" ]] \
    || ! confirm "Post to chat ${TELEGRAM_CHAT_ID}, the last to write?"; then
    ask TELEGRAM_CHAT_ID "The chat's id (a number, negative for a group):"
  fi
  if [[ ! "${TELEGRAM_CHAT_ID}" =~ ^-?[0-9]+$ ]]; then
    warn "${TELEGRAM_CHAT_ID} is not a chat's id"
    exit 1
  fi

  stage "Test the bot"
  local -r greeting="The cluster's alerts will arrive here."
  telegram sendMessage --data-urlencode "chat_id=${TELEGRAM_CHAT_ID}" \
    --data-urlencode "text=${greeting}" >/dev/null
  say "The bot has posted to the chat."
  if ! confirm "Did \"${greeting}\" arrive?"; then
    warn "check the chat's id and run the wizard again"
    exit 1
  fi

  stage "Make the Secret on the node"
  say "The Secret alertmanager-telegram in monitoring holds the token and the"
  say "chat's id. It must exist before the change that mounts it merges:"
  say "Alertmanager's pod does not start without it."
  note "A cluster-admin step: ClusterAdminUsed fires, by design."
  ask NODE "The node's SSH destination (a user with sudo):"
  confirm "Make or replace alertmanager-telegram on ${NODE}?" || exit 1
  on_node "$(secret_script)"
  say "alertmanager-telegram is on the node."
  pause

  stage "Check delivery"
  say "Once the change has merged and Flux has rolled Alertmanager out with"
  say "the Secret mounted, a test alert reaches the chat within a minute."
  if confirm "Has it merged and rolled out?"; then
    on_node "$(test_alert_script)"
    if ! confirm "Did AlertNotificationTest arrive in the chat?"; then
      SKIPPED+=("delivery: ${RUNBOOK}, Check delivery")
    fi
  else
    SKIPPED+=("after the merge: ${RUNBOOK}, Check delivery")
  fi

  finish
}

main "$@"
