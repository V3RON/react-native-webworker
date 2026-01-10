#!/bin/bash
# Thanks Marc! https://github.com/mrousavy/nitro/blob/main/scripts/kotlin-format.sh

set -e

KOTLIN_DIRS=(
  # Main Android source files
  "android"
)

if which ktlint >/dev/null; then
  ktlint --editorconfig=./config/.editorconfig --format "${KOTLIN_DIRS[@]}"
  echo "Kotlin Format done!"
else
  echo "error: ktlint not installed, install with 'brew install ktlint' (see https://github.com/pinterest/ktlint )"
  exit 1
fi
