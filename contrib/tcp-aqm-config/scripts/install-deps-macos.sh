#!/usr/bin/env bash
set -euo pipefail

if ! command -v brew >/dev/null 2>&1; then
  echo "Homebrew is required to install yaml-cpp on macOS." >&2
  echo "Install Homebrew from https://brew.sh/ and rerun this script." >&2
  exit 1
fi

brew install yaml-cpp fmt
