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
echo "==> Default admin: username=admin  password=admin123"
echo "------------------------------------------------------------"
exec ./voting_server "$PORT" web data
