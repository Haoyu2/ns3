#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

branch="$(git rev-parse --abbrev-ref HEAD)"
message="${1:-Update ICNS3 Overleaf paper}"

echo "Repository: $(pwd)"
echo "Branch: ${branch}"
echo

if [[ -z "$(git status --short)" ]]; then
  echo "No changes to commit."
  exit 0
fi

echo "Changes to commit:"
git status --short
echo

git add -A
git commit -m "${message}"
git pull --rebase origin "${branch}"
git push origin "${branch}"

echo
echo "Pushed ${branch} to origin."
