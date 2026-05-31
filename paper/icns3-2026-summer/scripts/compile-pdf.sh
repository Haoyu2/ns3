#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: ./scripts/compile-pdf.sh [--clean] [--figures]

Builds the local Overleaf PDF from main.tex.

Options:
  --clean    Remove LaTeX build artifacts with latexmk before compiling.
  --figures  Regenerate PDF figures from SVG files under fig/tcp-aqm/core-eval.
  -h, --help Show this help text.

If dependencies are missing, run:
  ./scripts/setup-latex-deps.sh
USAGE
}

has_cmd() {
  command -v "$1" >/dev/null 2>&1
}

require_cmd() {
  local name="$1"
  if ! has_cmd "$name"; then
    echo "Missing required command: ${name}" >&2
    echo "Run ./scripts/setup-latex-deps.sh first." >&2
    exit 1
  fi
}

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
paper_dir="$(cd "${script_dir}/.." && pwd)"

clean=0
figures=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --clean)
      clean=1
      ;;
    --figures)
      figures=1
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
  shift
done

cd "$paper_dir"

require_cmd latexmk
require_cmd pdflatex

echo "Paper directory: ${paper_dir}"

if [[ "$figures" -eq 1 ]]; then
  require_cmd rsvg-convert
  echo "Regenerating PDF figures from SVG files..."
  while IFS= read -r -d '' svg; do
    pdf="${svg%.svg}.pdf"
    echo "  ${svg} -> ${pdf}"
    rsvg-convert -f pdf -o "$pdf" "$svg"
  done < <(find fig/tcp-aqm/core-eval -type f -name '*.svg' -print0 | sort -z)
fi

if [[ "$clean" -eq 1 ]]; then
  echo "Cleaning LaTeX build artifacts..."
  latexmk -C main.tex
fi

echo "Compiling main.tex..."
latexmk -pdf -interaction=nonstopmode -halt-on-error main.tex

echo
echo "Built ${paper_dir}/main.pdf"
