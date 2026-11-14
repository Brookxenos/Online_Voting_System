#!/usr/bin/env bash
# Build and launch the Online Voting System.
set -e
cd "$(dirname "$0")"
PORT="${1:-8080}"
echo "==> Building voting_server ..."
make -s
mkdir -p data
echo "==> Launching on port $PORT"
echo "==> Open http://localhost:$PORT  in your browser"
echo "==> On first run, the initial admin password is printed once by the server."
echo "==> Set OV_INITIAL_ADMIN_PASSWORD before first run to choose that password."
echo "------------------------------------------------------------"
exec ./voting_server "$PORT" web data
