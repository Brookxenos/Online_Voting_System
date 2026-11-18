#!/usr/bin/env bash
# Keep the legacy nested entry point working by using the repository launcher.
set -e
exec "$(dirname "$0")/../start.sh" "$@"
