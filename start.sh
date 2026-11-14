#!/usr/bin/env bash
# Build and launch the Online Voting System.
set -e
cd "$(dirname "$0")"
PORT="${1:-${OV_PORT:-8080}}"
WEB_DIR="${OV_WEB_DIR:-web}"
DATA_DIR="${OV_DATA_DIR:-data}"
echo "==> Building voting_server ..."
make -s
if [ "${1:-}" = "--reset-admin-password" ]; then
  shift
  echo "==> Stop the server before resetting an administrator password."
  exec ./voting_server --reset-admin-password "$@"
fi
mkdir -p "$DATA_DIR"
echo "==> Launching on port $PORT"
echo "==> Open http://localhost:$PORT  in your browser"
echo "==> Web files: $WEB_DIR"
echo "==> Data directory: $DATA_DIR"
echo "==> On first run, the initial admin password is printed once by the server."
echo "==> Set OV_INITIAL_ADMIN_PASSWORD before first run to choose that password."
echo "------------------------------------------------------------"
exec ./voting_server "$PORT" "$WEB_DIR" "$DATA_DIR"
