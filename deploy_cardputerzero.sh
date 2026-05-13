#!/usr/bin/env bash
set -euo pipefail

HOST="${1:-${CARDPUTER_ZERO_HOST:-}}"
SSH_USER="${2:-${CARDPUTER_ZERO_USER:-pi}}"
if [ -z "$HOST" ]; then
  echo "usage: $0 <host> [ssh-user]" >&2
  echo "or set CARDPUTER_ZERO_HOST and optional CARDPUTER_ZERO_USER" >&2
  exit 2
fi

TARGET="${SSH_USER}@${HOST}"
REMOTE_DIR="/tmp/cardputerzero-calendar-deploy"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)"
export PATH="/opt/homebrew/bin:/usr/local/bin:${PATH}"

shell_quote() {
  printf "'"
  printf "%s" "$1" | sed "s/'/'\\\\''/g"
  printf "'"
}

if [ -n "${CARDPUTER_ZERO_SUDO_PASSWORD:-}" ]; then
  SUDO_PASSWORD_QUOTED="$(shell_quote "${CARDPUTER_ZERO_SUDO_PASSWORD}")"
  REMOTE_SUDO_FUNC="run_sudo() { printf '%s\n' ${SUDO_PASSWORD_QUOTED} | sudo -S \"\$@\"; }"
else
  REMOTE_SUDO_FUNC='run_sudo() { sudo -n "$@"; }'
fi

SSH_CMD=(ssh -o StrictHostKeyChecking=accept-new)
RSYNC_RSH="ssh -o StrictHostKeyChecking=accept-new"
if [ -n "${CARDPUTER_ZERO_PASSWORD:-}" ]; then
  if ! command -v sshpass >/dev/null 2>&1; then
    echo "CARDPUTER_ZERO_PASSWORD requires sshpass" >&2
    exit 2
  fi
  export SSHPASS="${CARDPUTER_ZERO_PASSWORD}"
  SSH_CMD=(sshpass -e ssh -o StrictHostKeyChecking=accept-new)
  RSYNC_RSH="sshpass -e ssh -o StrictHostKeyChecking=accept-new"
fi

cd "$(dirname "$0")"

CONFIG_REPO_AUTOMATION=1 CardputerZero=y scons -j"${JOBS}"

"${SSH_CMD[@]}" "${TARGET}" "rm -rf '${REMOTE_DIR}' && mkdir -p '${REMOTE_DIR}/bin' '${REMOTE_DIR}/applications' '${REMOTE_DIR}/share/images' '${REMOTE_DIR}/share/font'"
rsync -az -e "${RSYNC_RSH}" dist/M5CardputerZero-Calendar "${TARGET}:${REMOTE_DIR}/bin/"
rsync -az -e "${RSYNC_RSH}" applications/calendar.desktop "${TARGET}:${REMOTE_DIR}/applications/"
rsync -az -e "${RSYNC_RSH}" share/images/calendar.png "${TARGET}:${REMOTE_DIR}/share/images/"
if [ -f fonts/NotoSansSC-Regular.ttf ]; then
  rsync -az -e "${RSYNC_RSH}" fonts/NotoSansSC-Regular.ttf "${TARGET}:${REMOTE_DIR}/share/font/"
fi

"${SSH_CMD[@]}" "${TARGET}" "set -e
${REMOTE_SUDO_FUNC}
run_sudo mkdir -p /usr/share/APPLaunch/bin /usr/share/APPLaunch/applications /usr/share/APPLaunch/share/images /usr/share/APPLaunch/share/font
run_sudo install -m 0755 '${REMOTE_DIR}/bin/M5CardputerZero-Calendar' /usr/share/APPLaunch/bin/M5CardputerZero-Calendar
run_sudo install -m 0644 '${REMOTE_DIR}/applications/calendar.desktop' /usr/share/APPLaunch/applications/calendar.desktop
run_sudo install -m 0644 '${REMOTE_DIR}/share/images/calendar.png' /usr/share/APPLaunch/share/images/calendar.png
if [ -f '${REMOTE_DIR}/share/font/NotoSansSC-Regular.ttf' ]; then
  run_sudo install -m 0644 '${REMOTE_DIR}/share/font/NotoSansSC-Regular.ttf' /usr/share/APPLaunch/share/font/NotoSansSC-Regular.ttf
fi
run_sudo systemctl restart APPLaunch.service"

echo "deployed Calendar to ${TARGET}"
