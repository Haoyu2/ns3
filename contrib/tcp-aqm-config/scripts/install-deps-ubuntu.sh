#!/usr/bin/env bash
set -euo pipefail

if ! command -v apt-get >/dev/null 2>&1; then
  echo "apt-get is required by this helper. Install yaml-cpp with your system package manager." >&2
  exit 1
fi

sudo apt-get update
sudo apt-get install -y libyaml-cpp-dev libfmt-dev
