#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: ./scripts/setup-latex-deps.sh [--check]

Installs or checks the local dependencies needed to compile this Overleaf paper:
  - pdflatex
  - latexmk
  - rsvg-convert, used to regenerate vector PDF figures from SVG sources

Local build command:
  ./scripts/compile-pdf.sh
USAGE
}

mode="install"
if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  usage
  exit 0
fi
if [[ "${1:-}" == "--check" ]]; then
  mode="check"
fi

has_cmd() {
  command -v "$1" >/dev/null 2>&1
}

status_line() {
  local name="$1"
  if has_cmd "$name"; then
    printf "ok      %s -> %s\n" "$name" "$(command -v "$name")"
  else
    printf "missing %s\n" "$name"
  fi
}

check_deps() {
  status_line pdflatex
  status_line latexmk
  status_line rsvg-convert
}

missing_deps() {
  local missing=0
  for dep in pdflatex latexmk rsvg-convert; do
    if ! has_cmd "$dep"; then
      missing=1
    fi
  done
  return "$missing"
}

if [[ "$mode" == "check" ]]; then
  check_deps
  missing_deps
  exit $?
fi

os_name="$(uname -s)"
case "$os_name" in
  Darwin)
    if ! has_cmd brew; then
      echo "Homebrew is required for automatic macOS setup: https://brew.sh"
      exit 1
    fi
    if ! has_cmd pdflatex || ! has_cmd latexmk; then
      brew install texlive
    fi
    if ! has_cmd rsvg-convert; then
      brew install librsvg
    fi
    ;;
  Linux)
    if ! has_cmd apt-get; then
      echo "Automatic Linux setup currently supports apt-based distributions."
      echo "Install pdflatex, latexmk, texlive-publishers, texlive-latex-extra, and librsvg2-bin."
      exit 1
    fi
    sudo apt-get update
    sudo apt-get install -y \
      latexmk \
      texlive-latex-extra \
      texlive-publishers \
      texlive-fonts-recommended \
      texlive-bibtex-extra \
      cm-super \
      librsvg2-bin
    ;;
  *)
    echo "Unsupported OS: $os_name"
    echo "Install pdflatex, latexmk, and rsvg-convert manually."
    exit 1
    ;;
esac

echo
check_deps
echo
echo "Build with:"
echo "  ./scripts/compile-pdf.sh"
